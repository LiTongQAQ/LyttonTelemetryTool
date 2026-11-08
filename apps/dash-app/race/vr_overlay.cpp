#include "vr_overlay.hpp"

#include "log.hpp"

#include <windows.h>
#include <tlhelp32.h>

#include <openvr_capi.h>

#include <algorithm>
#include <cmath>

namespace vr_overlay {

namespace {

// SteamVR's server process is running.
bool steamVrRunning()
{
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return false;
    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof pe;
    bool found = false;
    for (BOOL ok = Process32FirstW(snap, &pe); ok && !found; ok = Process32NextW(snap, &pe))
        found = _wcsicmp(pe.szExeFile, L"vrserver.exe") == 0;
    CloseHandle(snap);
    return found;
}

using InitFn = intptr_t (*)(EVRInitError *, EVRApplicationType, const char *);
using ShutdownFn = void (*)();
using GetInterfaceFn = intptr_t (*)(const char *, EVRInitError *);
using IsInstalledFn = bool (*)();
using ErrorTextFn = const char *(*)(EVRInitError);

ShutdownFn g_shutdown = nullptr;

HmdMatrix34_t matrix(const Placement &p)
{
    const float yaw = p.yawDeg * 3.14159265f / 180.f, pitch = p.pitchDeg * 3.14159265f / 180.f;
    const float cy = std::cos(yaw), sy = std::sin(yaw), cp = std::cos(pitch), sp = std::sin(pitch);
    // R = Ry(yaw) * Rx(-pitch): yaw turns the panel about the vertical axis, pitch tilts its top away.
    HmdMatrix34_t m{};
    m.m[0][0] = cy;  m.m[0][1] = -sy * sp; m.m[0][2] = sy * cp; m.m[0][3] = p.x;
    m.m[1][0] = 0;   m.m[1][1] = cp;       m.m[1][2] = sp;      m.m[1][3] = p.y;
    m.m[2][0] = -sy; m.m[2][1] = -cy * sp; m.m[2][2] = cy * cp; m.m[2][3] = p.z;
    return m;
}

} // namespace

Overlays::~Overlays() { stop(); }

bool Overlays::start(std::vector<Panel> panels, std::string *why, bool launch)
{
    std::lock_guard<std::recursive_mutex> lock(m_); // submit() may run on the HMD render thread
    stop();
    if (!launch && !steamVrRunning()) {
        if (why) *why = "SteamVR is not running (start it, or press Launch SteamVR)";
        return false;
    }
    LOG(launch ? "SteamVR: launching / connecting" : "SteamVR: connecting");
    HMODULE dll = LoadLibraryA("openvr_api.dll");
    if (!dll) {
        if (why) *why = "openvr_api.dll not found next to the program";
        LOG("SteamVR: LoadLibrary(openvr_api.dll) failed, error %lu", GetLastError());
        return false;
    }
    auto installed = reinterpret_cast<IsInstalledFn>(GetProcAddress(dll, "VR_IsRuntimeInstalled"));
    auto init = reinterpret_cast<InitFn>(GetProcAddress(dll, "VR_InitInternal2"));
    auto get = reinterpret_cast<GetInterfaceFn>(GetProcAddress(dll, "VR_GetGenericInterface"));
    auto text = reinterpret_cast<ErrorTextFn>(GetProcAddress(dll, "VR_GetVRInitErrorAsEnglishDescription"));
    g_shutdown = reinterpret_cast<ShutdownFn>(GetProcAddress(dll, "VR_ShutdownInternal"));
    if (!installed || !init || !get || !g_shutdown) {
        if (why) *why = "openvr_api.dll is missing entry points";
        LOG("SteamVR: openvr_api.dll is missing entry points");
        FreeLibrary(dll);
        return false;
    }
    if (!installed()) {
        if (why) *why = "SteamVR is not installed";
        LOG("SteamVR: runtime not installed");
        FreeLibrary(dll);
        return false;
    }

    EVRInitError err = EVRInitError_VRInitError_None;
    init(&err, EVRApplicationType_VRApplication_Overlay, nullptr);
    if (err != EVRInitError_VRInitError_None) {
        if (why) *why = std::string("SteamVR: ") + (text ? text(err) : "init failed");
        LOG("SteamVR: init failed, error %d (%s)", int(err), text ? text(err) : "?");
        FreeLibrary(dll);
        return false;
    }
    std::string name = std::string("FnTable:") + IVROverlay_Version;
    auto *fn = reinterpret_cast<VR_IVROverlay_FnTable *>(get(name.c_str(), &err));
    if (!fn || err != EVRInitError_VRInitError_None) {
        if (why) *why = "SteamVR has no overlay interface";
        LOG("SteamVR: no overlay interface %s, error %d", IVROverlay_Version, int(err));
        g_shutdown();
        FreeLibrary(dll);
        return false;
    }
    dll_ = dll;
    fn_ = fn;
    std::string sysName = std::string("FnTable:") + IVRSystem_Version;
    if (void *sys = reinterpret_cast<void *>(get(sysName.c_str(), &err)); sys && err == EVRInitError_VRInitError_None) {
        sys_ = sys;
        readHmd(sys);
    } else {
        LOG("SteamVR: no system interface %s, error %d; headset parameters unknown", IVRSystem_Version, int(err));
    }
    panels_ = std::move(panels);
    for (int i = 0; i < int(panels_.size()); i++) {
        auto &p = panels_[i];
        VROverlayHandle_t h = 0;
        const EVROverlayError oe = fn_->CreateOverlay(const_cast<char *>(p.key.c_str()), const_cast<char *>(p.name.c_str()), &h);
        if (oe != EVROverlayError_VROverlayError_None)
            LOG("SteamVR: CreateOverlay %s failed, error %d (%s)", p.key.c_str(), int(oe), fn_->GetOverlayErrorNameFromEnum(oe));
        else
            LOG("SteamVR: overlay %s created, handle %llu, texture uv %.3f,%.3f - %.3f,%.3f", p.key.c_str(),
                (unsigned long long)h, p.u0, p.v0, p.u1, p.v1);
        p.handle = h;
        VRTextureBounds_t b{p.u0, p.v0, p.u1, p.v1};
        fn_->SetOverlayTextureBounds(h, &b);
        fn_->SetOverlaySortOrder(h, uint32_t(i));
        apply(i);
    }
    return true;
}

void Overlays::readHmd(void *system)
{
    auto *sys = static_cast<VR_IVRSystem_FnTable *>(system);
    HmdInfo h;
    sys->GetRecommendedRenderTargetSize(&h.renderW, &h.renderH);
    char buf[128] = {};
    ETrackedPropertyError pe;
    sys->GetStringTrackedDeviceProperty(TrackedDeviceIndex_t(k_unTrackedDeviceIndex_Hmd),
                                        ETrackedDeviceProperty_Prop_ModelNumber_String, buf, sizeof buf, &pe);
    h.model = buf;

    const float R2D = 180.f / 3.14159265f;
    float eyeL[2], eyeR[2], eyeUp[2], eyeDown[2];
    for (int e = 0; e < 2; e++) {
        float l, r, t, b;
        sys->GetProjectionRaw(EVREye(e), &l, &r, &t, &b); // tangents; OpenVR has "top" negative for up
        HmdMatrix34_t m = sys->GetEyeToHeadTransform(EVREye(e));
        // eye yaw in head space: angle of the eye's forward (-z) axis about the vertical
        const float cant = std::atan2(-m.m[0][2], m.m[2][2]) * R2D;
        eyeL[e] = cant + std::atan(l) * R2D;
        eyeR[e] = cant + std::atan(r) * R2D;
        eyeUp[e] = std::atan(-std::min(t, b)) * R2D;
        eyeDown[e] = -std::atan(std::max(t, b)) * R2D;
        if (e == 1) h.cantDeg = std::fabs(cant);
    }
    h.left = std::min(eyeL[0], eyeL[1]);
    h.right = std::max(eyeR[0], eyeR[1]);
    h.binoLeft = std::max(eyeL[0], eyeL[1]);
    h.binoRight = std::min(eyeR[0], eyeR[1]);
    h.up = std::min(eyeUp[0], eyeUp[1]);
    h.down = std::max(eyeDown[0], eyeDown[1]);
    h.eyeFovH = eyeR[0] - eyeL[0];
    h.valid = h.renderW > 0 && h.right > h.left;
    hmd_ = h;

    char maker[128] = {};
    sys->GetStringTrackedDeviceProperty(TrackedDeviceIndex_t(k_unTrackedDeviceIndex_Hmd),
                                        ETrackedDeviceProperty_Prop_ManufacturerName_String, maker, sizeof maker, &pe);
    const float hz = sys->GetFloatTrackedDeviceProperty(TrackedDeviceIndex_t(k_unTrackedDeviceIndex_Hmd),
                                                        ETrackedDeviceProperty_Prop_DisplayFrequency_Float, &pe);
    const float ipd = sys->GetFloatTrackedDeviceProperty(TrackedDeviceIndex_t(k_unTrackedDeviceIndex_Hmd),
                                                         ETrackedDeviceProperty_Prop_UserIpdMeters_Float, &pe);
    LOG("HMD: %s / %s, %.0f Hz, IPD %.1f mm, render %u x %u per eye", maker, h.model.c_str(), hz, ipd * 1000.f,
        h.renderW, h.renderH);
    for (int e = 0; e < 2; e++) {
        float l, r, t, b;
        sys->GetProjectionRaw(EVREye(e), &l, &r, &t, &b);
        HmdMatrix34_t m = sys->GetEyeToHeadTransform(EVREye(e));
        LOG("HMD: %s eye projection raw l %.4f r %.4f t %.4f b %.4f; eye-to-head row0 %.4f %.4f %.4f %.4f "
            "row1 %.4f %.4f %.4f %.4f row2 %.4f %.4f %.4f %.4f",
            e ? "right" : "left", l, r, t, b, m.m[0][0], m.m[0][1], m.m[0][2], m.m[0][3], m.m[1][0], m.m[1][1],
            m.m[1][2], m.m[1][3], m.m[2][0], m.m[2][1], m.m[2][2], m.m[2][3]);
    }
    LOG("HMD: fov left %.1f right %.1f up %.1f down %.1f, both eyes %.1f .. %.1f, eye fov %.1f, cant %.1f, "
        "%.1f px/deg%s",
        h.left, h.right, h.up, h.down, h.binoLeft, h.binoRight, h.eyeFovH, h.cantDeg, h.ppd(), h.valid ? "" : " (INVALID)");
}

void Overlays::stop()
{
    std::lock_guard<std::mutex> w(wait_); // after the HMD thread's frame wait (at most its timeout)
    std::lock_guard<std::recursive_mutex> lock(m_); // submit() may run on the HMD render thread
    if (fn_) {
        for (auto &p : panels_) if (p.handle) fn_->DestroyOverlay(p.handle);
        if (g_shutdown) g_shutdown();
        LOG("SteamVR: disconnected");
    }
    fn_ = nullptr;
    sys_ = nullptr;
    if (dll_) FreeLibrary(static_cast<HMODULE>(dll_));
    dll_ = nullptr;
}

void Overlays::apply(int i)
{
    auto &p = panels_[i];
    if (!fn_ || !p.handle) return;
    fn_->SetOverlayWidthInMeters(p.handle, p.place.widthM);
    fn_->SetOverlayAlpha(p.handle, alpha_);
    HmdMatrix34_t m = matrix(p.place);
    if (p.place.headLocked)
        fn_->SetOverlayTransformTrackedDeviceRelative(p.handle, TrackedDeviceIndex_t(k_unTrackedDeviceIndex_Hmd), &m);
    else
        fn_->SetOverlayTransformAbsolute(p.handle, ETrackingUniverseOrigin_TrackingUniverseSeated, &m);
    if (p.place.visible) fn_->ShowOverlay(p.handle);
    else fn_->HideOverlay(p.handle);
}

void Overlays::setPlacement(int i, const Placement &p)
{
    std::lock_guard<std::recursive_mutex> lock(m_); // submit() may run on the HMD render thread
    if (i < 0 || i >= int(panels_.size())) return;
    panels_[i].place = p;
    apply(i);
}

void Overlays::setOpacity(float alpha)
{
    std::lock_guard<std::recursive_mutex> lock(m_); // submit() may run on the HMD render thread
    alpha_ = std::clamp(alpha, 0.f, 1.f);
    if (!fn_) return;
    for (auto &p : panels_)
        if (p.handle) fn_->SetOverlayAlpha(p.handle, alpha_);
}

void Overlays::submit(ID3D11Texture2D *tex)
{
    std::lock_guard<std::recursive_mutex> lock(m_); // submit() may run on the HMD render thread
    if (!fn_) return;
    Texture_t t{tex, ETextureType_TextureType_DirectX, EColorSpace_ColorSpace_Auto};
    for (auto &p : panels_) {
        if (!p.handle || !p.place.visible) continue;
        const int e = int(fn_->SetOverlayTexture(p.handle, &t));
        if (e != p.texError) {
            if (e) LOG("SteamVR: SetOverlayTexture %s error %d (%s)", p.key.c_str(), e,
                       fn_->GetOverlayErrorNameFromEnum(EVROverlayError(e)));
            else LOG("SteamVR: SetOverlayTexture %s ok again", p.key.c_str());
            p.texError = e;
        }
    }
}

bool Overlays::waitFrame(uint32_t timeoutMs)
{
    std::lock_guard<std::mutex> w(wait_);
    decltype(fn_) fn;
    {
        std::lock_guard<std::recursive_mutex> lock(m_);
        fn = fn_;
    }
    if (!fn || !fn->WaitFrameSync) return false; // not under the lock: the GUI thread must not wait a frame for poll()
    return fn->WaitFrameSync(timeoutMs) == EVROverlayError_VROverlayError_None;
}

bool Overlays::poll()
{
    std::lock_guard<std::recursive_mutex> lock(m_); // submit() may run on the HMD render thread
    if (!fn_ || !sys_) return fn_ != nullptr;
    auto *sys = static_cast<VR_IVRSystem_FnTable *>(sys_);
    VREvent_t ev;
    while (sys->PollNextEvent(&ev, sizeof ev)) {
        if (ev.eventType == EVREventType_VREvent_Quit) {
            LOG("SteamVR: quit requested by SteamVR");
            sys->AcknowledgeQuit_Exiting();
            stop();
            return false;
        }
    }
    return true;
}

} // namespace vr_overlay
