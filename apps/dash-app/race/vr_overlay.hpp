// SteamVR overlays through OpenVR's C function table (ABI-safe with clang/MinGW).
//
// openvr_api.dll is loaded at run time: without SteamVR the app keeps running with only the desktop window.
// Each panel is its own overlay showing a sub-rectangle of one shared D3D11 texture, placed in the seated
// tracking space (origin = where the head was at the last "reset seated position").
#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

struct ID3D11Texture2D;
struct VR_IVROverlay_FnTable;

namespace vr_overlay {

struct Placement {
    float x = 0, y = -0.2f, z = -0.6f; // metres: +x right, +y up, -z forward
    float yawDeg = 0, pitchDeg = 0;    // turn left/right, tilt back
    float widthM = 0.3f;
    bool visible = true;
    bool headLocked = false; // true: relative to the HMD (moves with the head); false: seated space (fixed in the cockpit)
};

// What the headset can show, in head space (degrees: left/down negative). Read from SteamVR at start.
struct HmdInfo {
    bool valid = false;
    std::string model;
    uint32_t renderW = 0, renderH = 0; // recommended render size per eye (includes SteamVR supersampling)
    float left = -50, right = 50, up = 50, down = -50; // total field of view over both eyes
    float binoLeft = -45, binoRight = 45;              // horizontal range both eyes see
    float eyeFovH = 100;                               // one eye's horizontal field of view
    float cantDeg = 0;                                 // outward rotation of each eye's display (canted panels)
    float ppd() const { return eyeFovH > 1 ? float(renderW) / eyeFovH : 0; } // render pixels per degree
};

struct Panel {
    std::string key, name;
    float u0, v0, u1, v1; // region of the shared texture
    Placement place;
    uint64_t handle = 0;
    int texError = 0; // last SetOverlayTexture error, logged when it changes
};

class Overlays {
public:
    ~Overlays();
    // Loads SteamVR and creates the overlays. False with a reason when SteamVR is not available. Without `launch`
    // only connects to a SteamVR that is already running (connecting as an overlay would start it otherwise).
    bool start(std::vector<Panel> panels, std::string *why, bool launch = false);
    void stop();
    bool running() const { return fn_ != nullptr; }
    const HmdInfo &hmd() const { return hmd_; }

    void setPlacement(int panel, const Placement &p);
    void setOpacity(float alpha); // 0..1, all panels
    const std::vector<Panel> &panels() const { return panels_; }
    void submit(ID3D11Texture2D *tex); // every frame
    // blocks until SteamVR starts its next frame (overlay frame sync); false: not available, pace yourself
    bool waitFrame(uint32_t timeoutMs);
    // every frame: handles SteamVR events. Returns false when SteamVR has quit (the overlays are then stopped).
    bool poll();

private:
    void apply(int i);
    void readHmd(void *system);
    HmdInfo hmd_;
    float alpha_ = 1;
    void *dll_ = nullptr;
    ::VR_IVROverlay_FnTable *fn_ = nullptr;
    std::mutex wait_; // held through waitFrame(): stop() must not unload SteamVR under it
    std::recursive_mutex m_; // the Qt program submits from the HMD render thread
    void *sys_ = nullptr; // VR_IVRSystem_FnTable
    std::vector<Panel> panels_;
};

} // namespace vr_overlay
