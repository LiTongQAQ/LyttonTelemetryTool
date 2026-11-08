// See engine.hpp. Ported from the ImGui program's main.cpp (HMD part only).
#include "engine.hpp"

#include "log.hpp"

#include <imgui.h>
#include <imgui_impl_dx11.h>

#include <d3d11.h>
#include <windows.h>

#include <timeapi.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <deque>
#include <filesystem>
#include <vector>

namespace engine {

namespace fs = std::filesystem;
using layout::PANELS;

std::string exeDir()
{
    wchar_t buf[MAX_PATH];
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return fs::path(buf).parent_path().string();
}

std::string dataDir()
{
    const fs::path exe = exeDir();
    std::error_code ec;
    const fs::path probe = exe / ".lytton-write-test";
    if (FILE *f = _wfopen(probe.wstring().c_str(), L"wb")) {
        std::fclose(f);
        fs::remove(probe, ec);
        return exe.string();
    }
    wchar_t buf[MAX_PATH];
    const DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", buf, MAX_PATH);
    if (!n || n >= MAX_PATH) return exe.string();
    const fs::path dir = fs::path(buf) / "LyttonTelemetryTool";
    fs::create_directories(dir, ec);
    return dir.string();
}

namespace {

std::string g_latinFont;
bool g_latinDigits = true;

// A font file's cap height as a share of hhea ascent - descent, 0 when unknown.
float capShare(const std::string &path)
{
    std::FILE *f = _wfopen(fs::u8path(path).wstring().c_str(), L"rb");
    if (!f) return 0;
    std::vector<uint8_t> d;
    uint8_t buf[65536];
    for (size_t n; (n = std::fread(buf, 1, sizeof buf, f)) > 0 && d.size() < (64u << 20);) d.insert(d.end(), buf, buf + n);
    std::fclose(f);
    auto u16 = [&](size_t o) { return o + 2 <= d.size() ? unsigned(d[o] << 8 | d[o + 1]) : 0u; };
    auto u32 = [&](size_t o) { return o + 4 <= d.size() ? uint32_t(u16(o) << 16 | u16(o + 2)) : 0u; };
    size_t base = d.size() >= 16 && std::memcmp(d.data(), "ttcf", 4) == 0 ? u32(12) : 0;
    size_t hhea = 0, os2 = 0;
    for (unsigned i = 0, n = u16(base + 4); i < n; i++) {
        const size_t e = base + 12 + 16 * i;
        if (e + 16 > d.size()) break;
        if (std::memcmp(&d[e], "hhea", 4) == 0) hhea = u32(e + 8);
        if (std::memcmp(&d[e], "OS/2", 4) == 0) os2 = u32(e + 8);
    }
    if (!hhea || !os2 || u16(os2) < 2) return 0;
    const float asc = float(int16_t(u16(hhea + 4))), desc = float(int16_t(u16(hhea + 6))), cap = float(int16_t(u16(os2 + 88)));
    return asc - desc > 0 && cap > 0 ? cap / (asc - desc) : 0;
}

} // namespace

// The first existing file of `files` at `px`, with the Latin override font merged in (see the old main.cpp).
ImFont *addFont(ImGuiIO &io, std::initializer_list<const char *> files, float px, const ImWchar *ranges)
{
    static const ImWchar letters[] = {'A', 'Z', 'a', 'z', 0},
                         lettersDigits[] = {0x20, 0x7E, 0xA0, 0xFF, 0x2010, 0x2027, 0x2030, 0x205E, 0x2190, 0x2193, 0};
    const char *file = nullptr;
    for (const char *f : files)
        if (fs::exists(f)) { file = f; break; }
    if (!ranges) ranges = io.Fonts->GetGlyphRangesDefault();
    if (!file) {
        ImFontConfig cfg;
        cfg.SizePixels = px;
        return io.Fonts->AddFontDefault(&cfg);
    }
    const bool over = !g_latinFont.empty() && fs::exists(fs::u8path(g_latinFont));
    if (!over) return io.Fonts->AddFontFromFileTTF(file, px, nullptr, ranges);
    const ImWchar *repl = g_latinDigits ? lettersDigits : letters;
    static std::deque<ImVector<ImWchar>> keep;
    ImFontGlyphRangesBuilder b;
    b.AddRanges(ranges);
    for (const ImWchar *r = repl; r[0]; r += 2)
        for (unsigned c = r[0]; c <= r[1]; c++) b.UsedChars[int(c >> 5)] &= ~(ImU32(1) << (c & 31));
    keep.emplace_back();
    b.BuildRanges(&keep.back());
    ImFont *f = io.Fonts->AddFontFromFileTTF(file, px, nullptr, keep.back().Data);
    const float cb = capShare(file), co = capShare(g_latinFont);
    const float k = cb > 0 && co > 0 ? std::clamp(cb / co, 0.5f, 3.f) : 1.f;
    ImFontConfig cfg;
    cfg.MergeMode = true;
    io.Fonts->AddFontFromFileTTF(g_latinFont.c_str(), px * k, &cfg, repl);
    io.Fonts->AddFontFromFileTTF(file, px, &cfg, ranges);
    return f;
}

namespace {

bool sameOffset(const layout::Offset &a, const layout::Offset &b)
{
    return a.daz == b.daz && a.del == b.del && a.dz == b.dz && a.dyaw == b.dyaw && a.dpitch == b.dpitch &&
           a.scale == b.scale && a.visible == b.visible && a.cockpit == b.cockpit && a.cx == b.cx &&
           a.cy == b.cy && a.cz == b.cz;
}

void logPanel(int i, const layout::Fit &f, const layout::Offset &o, const vr_overlay::Placement &p)
{
    static const char *names[PANELS] = {"tower", "sectors", "pedals", "car", "status", "weather"};
    LOG("panel %s: base az %.1f el %.1f size %.1f x %.1f deg; offset az %+.1f el %+.1f dz %+.2f yaw %+.0f pitch %+.0f "
        "scale %.2f %s%s; placed x %+.3f y %+.3f z %+.3f yaw %+.1f pitch %+.1f width %.3f m%s",
        names[i], f.azDeg, f.elDeg, f.widthDeg, f.heightDeg, o.daz, o.del, o.dz, o.dyaw, o.dpitch, o.scale,
        o.visible ? "shown" : "hidden", o.cockpit ? ", fixed in cockpit" : "", p.x, p.y, p.z, p.yawDeg, p.pitchDeg,
        p.widthM, p.headLocked ? ", head locked" : ", seated space");
}

} // namespace

Engine::Engine(app::App &app) : a(app)
{
    g_latinFont = a.cfg.latinFont;
    g_latinDigits = a.cfg.latinDigits;
    for (int i = 0; i < PANELS; i++) savedOffset_[i] = a.cfg.offset[i];
}

Engine::~Engine()
{
    hmdRun_ = false;
    if (hmdThread_.joinable()) hmdThread_.join();
    a.stopRecording();
    vr.stop();
    if (imgui_) {
        ImGui::SetCurrentContext(imgui_);
        ImGui_ImplDX11_Shutdown();
        ImGui::DestroyContext(imgui_);
    }
    for (auto &u : uploads_) {
        if (u.second.srv) u.second.srv->Release();
        if (u.second.tex) u.second.tex->Release();
    }
    for (IUnknown *p : std::initializer_list<IUnknown *>{staging_, rtv_, tex_, ctx_, dev_})
        if (p) p->Release();
}

void Engine::refit()
{
    hmd = layout::applyOverrides(vr.hmd(), a.cfg.hmd);
    layoutHmd = layout::applyViewBox(hmd, a.cfg.hmd);
    layout::fitAll(layoutHmd, a.cfg.offset, fits);
    for (int i = 0; i < PANELS; i++) vr.setPlacement(i, layout::place(fits[i], a.cfg.offset[i]));
}

void Engine::startVr(bool launch)
{
    std::vector<vr_overlay::Panel> list;
    const panels::Rect rects[PANELS] = {panels::TOWER, panels::SECTORS, panels::PEDALS, panels::CAR, panels::STATUS, panels::WEATHER};
    const char *keys[PANELS] = {"lytton.f1race.tower", "lytton.f1race.sectors", "lytton.f1race.pedals",
                                "lytton.f1race.car", "lytton.f1race.status", "lytton.f1race.weather"};
    const char *names[PANELS] = {"F1 Timing", "F1 Sectors", "F1 Pedals", "F1 Car", "F1 Status", "F1 Weather"};
    const float T = float(panels::TEX);
    for (int i = 0; i < PANELS; i++) {
        const auto &r = rects[i];
        list.push_back({keys[i], names[i], r.x / T, r.y / T, (r.x + r.w) / T, (r.y + r.h) / T, {}});
    }
    if (vr.start(std::move(list), &vrStatus, launch)) vrStatus = "SteamVR connected";
    vr.setOpacity(1.f);
    refit();
    if (vr.running()) LOG("SteamVR: connected");
    else LOG("SteamVR: not connected (%s), panels use default headset values", vrStatus.c_str());
    LOG("HMD used for layout: fov left %.1f right %.1f up %.1f down %.1f, %.1f px/deg", hmd.left, hmd.right, hmd.up,
        hmd.down, hmd.ppd());
    for (int i = 0; i < PANELS; i++) logPanel(i, fits[i], a.cfg.offset[i], layout::place(fits[i], a.cfg.offset[i]));
}

bool Engine::init()
{
    startVr(false); // never starts SteamVR by itself
    texScale = layout::textureScale(hmd);
    texSize = unsigned(float(panels::TEX) * texScale);
    LOG("panel texture %u x %u (scale %.2f)", texSize, texSize, texScale);

    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0};
    if (D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, 1, D3D11_SDK_VERSION, &dev_, nullptr,
                          &ctx_) != S_OK) {
        LOG("D3D11 device for the HMD panels could not be created");
        return false;
    }
    D3D11_TEXTURE2D_DESC td{};
    td.Width = td.Height = texSize;
    td.MipLevels = td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    if (dev_->CreateTexture2D(&td, nullptr, &tex_) != S_OK) return false;
    dev_->CreateRenderTargetView(tex_, nullptr, &rtv_);

    lastVrTry_ = lastSave_ = std::chrono::steady_clock::now();
    snap = a.race.snapshot();
    hmdRun_ = true;
    hmdThread_ = std::thread([this] { hmdLoop(); });
    return true;
}

// The HMD thread: its own ImGui context (the current context is per thread, see imgui_tls.hpp), a frame every
// 1/90 s from the latest race state, straight to SteamVR.
void Engine::hmdLoop()
{
    timeBeginPeriod(1); // 1 ms sleeps, for an even frame pace
    imgui_ = ImGui::CreateContext();
    ImGui::SetCurrentContext(imgui_);
    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2(float(panels::TEX), float(panels::TEX));
    const auto faces = {"C:/Windows/Fonts/bahnschrift.ttf", "C:/Windows/Fonts/segoeuib.ttf", "C:/Windows/Fonts/arialbd.ttf"};
    pf_.scale = texScale;
    pf_.body = addFont(io, faces, 30.f * texScale);
    pf_.small = addFont(io, faces, 20.f * texScale);
    pf_.big = addFont(io, faces, 52.f * texScale);
    {
        std::lock_guard<std::mutex> l(d3d_);
        ImGui_ImplDX11_Init(dev_, ctx_);
    }
    LOG("HMD panels: own thread, in step with SteamVR frames (90 Hz without)");
    using clock = std::chrono::steady_clock;
    const auto period = std::chrono::microseconds(11111);
    auto next = clock::now(), last = next, fpsT = next;
    int frames = 0;
    while (hmdRun_) {
        const auto now = clock::now();
        const float dt = std::chrono::duration<float>(now - last).count();
        last = now;
        const timing::Snapshot s = a.race.snapshot();
        {
            std::lock_guard<std::mutex> l(d3d_);
            renderPanels(s, dt);
        }
        frames++;
        if (now - fpsT >= std::chrono::seconds(1)) {
            hmdFps_ = frames / std::chrono::duration<double>(now - fpsT).count();
            frames = 0;
            fpsT = now;
        }
        // in step with SteamVR's own frames (the headset may run at 72, 90 or 120 Hz): a fixed 90 Hz timer beats
        // against the display and the pedals and timers judder; without SteamVR, the timer
        if (vr.running() && vr.waitFrame(50)) {
            next = clock::now();
            continue;
        }
        next += period;
        if (next < clock::now()) next = clock::now(); // fell behind: don't try to catch up
        std::this_thread::sleep_until(next);
    }
    {
        std::lock_guard<std::mutex> l(d3d_);
        ImGui_ImplDX11_Shutdown();
    }
    ImGui::DestroyContext(imgui_);
    imgui_ = nullptr;
    timeEndPeriod(1);
}

void Engine::tick(float dt)
{
    const auto now = std::chrono::steady_clock::now();
    a.update();
    if (vr.running() && !vr.poll()) vrStatus = "SteamVR quit; waiting to reconnect";
    if (!vr.running() && now - lastVrTry_ > std::chrono::seconds(5)) {
        lastVrTry_ = now;
        vrTries_++;
        applog::hold();
        startVr(false);
        applog::release(vr.running());
        if (vr.running()) {
            LOG("SteamVR: connected on retry %d", vrTries_);
            if (layout::textureScale(hmd) != texScale)
                LOG("panel texture was sized for the default headset (scale %.2f, this one wants %.2f); restart for full sharpness",
                    texScale, layout::textureScale(hmd));
            vrTries_ = 0;
        }
    }
    if (a.dirty && now - lastSave_ > std::chrono::seconds(1)) {
        for (int i = 0; i < PANELS; i++)
            if (!sameOffset(a.cfg.offset[i], savedOffset_[i])) {
                logPanel(i, fits[i], a.cfg.offset[i], layout::place(fits[i], a.cfg.offset[i]));
                savedOffset_[i] = a.cfg.offset[i];
            }
        a.save();
        lastSave_ = now;
    }
    snap = a.race.snapshot();
    (void)dt;
    static auto fpsLog = now;
    if (now - fpsLog > std::chrono::seconds(30)) { // the HMD thread's pace in the log
        fpsLog = now;
        LOG("HMD panels: %.1f frames/s", hmdFps_.load());
    }
}

bool Engine::inPanelFrame() const { return imgui_ && ImGui::GetCurrentContext() == imgui_; }

void *Engine::uploadTexture(const std::string &slot, const unsigned char *rgba, int w, int h)
{
    if (!dev_) return nullptr;
    Upload &u = uploads_[slot];
    if (!u.tex || u.w != w || u.h != h) {
        if (u.srv) u.srv->Release();
        if (u.tex) u.tex->Release();
        u = {};
        D3D11_TEXTURE2D_DESC td{};
        td.Width = UINT(w);
        td.Height = UINT(h);
        td.MipLevels = td.ArraySize = 1;
        td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        td.SampleDesc.Count = 1;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        if (dev_->CreateTexture2D(&td, nullptr, &u.tex) != S_OK) return nullptr;
        dev_->CreateShaderResourceView(u.tex, nullptr, &u.srv);
        u.w = w;
        u.h = h;
    }
    ctx_->UpdateSubresource(u.tex, 0, nullptr, rgba, UINT(w) * 4, 0);
    return u.srv;
}

bool Engine::readPanel(std::vector<uint8_t> &rgba, unsigned &size)
{
    if (!dev_ || !tex_) return false;
    std::lock_guard<std::mutex> l(d3d_);
    if (!staging_) {
        D3D11_TEXTURE2D_DESC td{};
        tex_->GetDesc(&td);
        td.BindFlags = 0;
        td.Usage = D3D11_USAGE_STAGING;
        td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        if (dev_->CreateTexture2D(&td, nullptr, &staging_) != S_OK) return false;
    }
    ctx_->CopyResource(staging_, tex_);
    D3D11_MAPPED_SUBRESOURCE m{};
    if (ctx_->Map(staging_, 0, D3D11_MAP_READ, 0, &m) != S_OK) return false;
    size = texSize;
    rgba.resize(size_t(size) * size * 4);
    for (unsigned y = 0; y < size; y++)
        std::memcpy(&rgba[size_t(y) * size * 4], static_cast<const uint8_t *>(m.pData) + size_t(y) * m.RowPitch, size_t(size) * 4);
    ctx_->Unmap(staging_, 0);
    return true;
}

void Engine::renderPanels(const timing::Snapshot &s, float dt)
{
    ImGui::SetCurrentContext(imgui_);
    ImGui_ImplDX11_NewFrame();
    ImGui::GetIO().DeltaTime = std::max(1e-4f, dt);
    ImGui::NewFrame();
    panels::draw(s, pf_, panels::GapMode(a.cfg.gapMode));
    ImGui::Render();
    panels::applyOpacity(ImGui::GetDrawData(), a.cfg.bgOpacity, a.cfg.textOpacity);
    if (ImDrawData *dd = ImGui::GetDrawData(); texScale != 1.f) {
        for (ImDrawList *l : dd->CmdLists)
            for (ImDrawVert &v : l->VtxBuffer) { v.pos.x *= texScale; v.pos.y *= texScale; }
        dd->ScaleClipRects(ImVec2(texScale, texScale));
        dd->DisplaySize = ImVec2(float(texSize), float(texSize));
    }
    const float clear[4] = {0, 0, 0, 0};
    ctx_->OMSetRenderTargets(1, &rtv_, nullptr);
    ctx_->ClearRenderTargetView(rtv_, clear);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    ctx_->Flush(); // no swap chain on this thread to Present: push the frame to the GPU now, or it waits in the driver
    if (vr.running()) vr.submit(tex_);
}

} // namespace engine
