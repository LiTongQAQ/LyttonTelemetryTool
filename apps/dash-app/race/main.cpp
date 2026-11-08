// f1-race: the program that runs next to the game.
//
// The hub receives the telemetry; RaceState turns it into standings, sectors and inputs; the HMD panels are drawn
// into one texture that SteamVR shows as overlays. The desktop window previews that texture and holds the settings
// (panel placement, hub ports, recording). Without SteamVR it runs desktop-only.
//
//   f1-race [--port 20700] [--replay file.f1tr] [--demo]
#include "hub.hpp"
#include "layout.hpp"
#include "log.hpp"
#include "single.hpp"
#include "panels.hpp"
#include "race_state.hpp"
#include "vr_overlay.hpp"
#include "app.hpp"
#include "ui.hpp"

#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>
#include <imgui_internal.h>

#include <d3d11.h>
#include <shellapi.h>
#include <windows.h>

#include <array>
#include <atomic>
#include <cmath>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <cstring>
#include <deque>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <thread>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

#ifndef F1RACE_VERSION
#define F1RACE_VERSION "dev"
#endif
#define F1RACE_WIDEN2(x) L##x
#define F1RACE_WIDEN(x) F1RACE_WIDEN2(x)
#define F1RACE_VERSION_W F1RACE_WIDEN(F1RACE_VERSION)

namespace {

// ---- D3D11 -----------------------------------------------------------------------------------------------------
ID3D11Device *g_dev = nullptr;
ID3D11DeviceContext *g_ctx = nullptr;
IDXGISwapChain *g_swap = nullptr;
ID3D11RenderTargetView *g_backRtv = nullptr;
ID3D11Texture2D *g_panelTex = nullptr;
ID3D11RenderTargetView *g_panelRtv = nullptr;
ID3D11ShaderResourceView *g_panelSrv = nullptr;
ImGuiContext *g_deskCtx = nullptr;
UINT g_resizeW = 0, g_resizeH = 0;

void createBackRtv()
{
    ID3D11Texture2D *back = nullptr;
    g_swap->GetBuffer(0, IID_PPV_ARGS(&back));
    g_dev->CreateRenderTargetView(back, nullptr, &g_backRtv);
    back->Release();
}

bool createDevice(HWND hwnd, UINT texSize)
{
    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 2;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0};
    if (D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, 1, D3D11_SDK_VERSION, &sd,
                                      &g_swap, &g_dev, nullptr, &g_ctx) != S_OK)
        return false;
    createBackRtv();

    D3D11_TEXTURE2D_DESC td{};
    td.Width = td.Height = texSize;
    td.MipLevels = td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    if (g_dev->CreateTexture2D(&td, nullptr, &g_panelTex) != S_OK) return false;
    g_dev->CreateRenderTargetView(g_panelTex, nullptr, &g_panelRtv);
    g_dev->CreateShaderResourceView(g_panelTex, nullptr, &g_panelSrv);
    return true;
}

void destroyDevice()
{
    for (IUnknown *p : std::initializer_list<IUnknown *>{g_panelSrv, g_panelRtv, g_panelTex, g_backRtv, g_swap, g_ctx, g_dev})
        if (p) p->Release();
}

LRESULT WINAPI wndProc(HWND h, UINT msg, WPARAM w, LPARAM l)
{
    if (g_deskCtx) {
        ImGuiContext *prev = ImGui::GetCurrentContext();
        ImGui::SetCurrentContext(g_deskCtx);
        LRESULT r = ImGui_ImplWin32_WndProcHandler(h, msg, w, l);
        ImGui::SetCurrentContext(prev);
        if (r) return r;
    }
    switch (msg) {
    case WM_SIZE:
        if (w != SIZE_MINIMIZED) { g_resizeW = LOWORD(l); g_resizeH = HIWORD(l); }
        return 0;
    case WM_SYSCOMMAND:
        if ((w & 0xfff0) == SC_KEYMENU) return 0;
        break;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(h, msg, w, l);
}

std::string exeDir()
{
    wchar_t buf[MAX_PATH];
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return std::filesystem::path(buf).parent_path().string();
}

// Where settings and recordings go: next to the exe (portable use, development), or %LOCALAPPDATA%\F1Race when
// that folder is not writable (installed under Program Files).
std::string dataDir()
{
    namespace fs = std::filesystem;
    const fs::path exe = exeDir();
    std::error_code ec;
    const fs::path probe = exe / ".f1race-write-test";
    if (FILE *f = _wfopen(probe.wstring().c_str(), L"wb")) {
        std::fclose(f);
        fs::remove(probe, ec);
        return exe.string();
    }
    wchar_t buf[MAX_PATH];
    const DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", buf, MAX_PATH);
    if (!n || n >= MAX_PATH) return exe.string();
    const fs::path dir = fs::path(buf) / "F1Race";
    fs::create_directories(dir, ec);
    return dir.string();
}

using layout::PANELS;

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
        o.visible ? "shown" : "hidden", o.cockpit ? ", fixed in cockpit" : "", p.x, p.y, p.z, p.yawDeg, p.pitchDeg, p.widthM, p.headLocked ? ", head locked" : ", seated space");
}

// ---- fixed design size ---------------------------------------------------------------------------------------------
// The desktop UI is laid out for one size (the development machine's maximised window) and scaled as a whole to the
// real window, keeping its proportions, so it looks the same at any resolution. Fonts are baked at the scale the
// program starts with, so text stays sharp.
constexpr float UI_W = 1920.f, UI_H = 1009.f;
struct UiScale {
    float s = 1, offX = 0, offY = 0;
};
UiScale uiScale(float w, float h)
{
    UiScale u;
    if (w < 1 || h < 1) return u;
    u.s = std::min(w / UI_W, h / UI_H);
    u.offX = std::floor((w - UI_W * u.s) / 2);
    u.offY = std::floor((h - UI_H * u.s) / 2);
    return u;
}

std::string g_latinFont; // settings: a font for the Latin letters (A-Z, a-z) of every font, empty = none
bool g_latinDigits = true; // ... and digits, signs and Latin-1 (°, ±, ·) too, wherever that font has them

// A font file's cap height as a share of the height ImGui sizes it by (hhea ascent - descent), 0 when unknown.
float capShare(const std::string &path)
{
    std::FILE *f = _wfopen(std::filesystem::u8path(path).wstring().c_str(), L"rb");
    if (!f) return 0;
    std::vector<uint8_t> d;
    uint8_t buf[65536];
    for (size_t n; (n = std::fread(buf, 1, sizeof buf, f)) > 0 && d.size() < (64u << 20);) d.insert(d.end(), buf, buf + n);
    std::fclose(f);
    auto u16 = [&](size_t o) { return o + 2 <= d.size() ? unsigned(d[o] << 8 | d[o + 1]) : 0u; };
    auto u32 = [&](size_t o) { return o + 4 <= d.size() ? uint32_t(u16(o) << 16 | u16(o + 2)) : 0u; };
    size_t base = d.size() >= 16 && std::memcmp(d.data(), "ttcf", 4) == 0 ? u32(12) : 0; // a collection: its first font
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

// The first of `files` that exists at `px` pixels. With g_latinFont set, the letters (with g_latinDigits also digits,
// signs, Latin-1 and common punctuation) come from that font where it has it: the base font stays the first source,
// so the line height and baseline are its own, the replaced characters are left out of it, the replacement font is
// merged sized so its capitals are as tall as the base font's (ROG Fonts' are half as tall at the same size), and the
// base font is merged once more for whatever the replacement lacks.
ImFont *addFontPx(ImGuiIO &io, std::initializer_list<const char *> files, float px, const ImWchar *ranges)
{
    static const ImWchar letters[] = {'A', 'Z', 'a', 'z', 0},
                         lettersDigits[] = {0x20, 0x7E, 0xA0, 0xFF, 0x2010, 0x2027, 0x2030, 0x205E, 0x2190, 0x2193, 0};
    const char *file = nullptr;
    for (const char *f : files)
        if (std::filesystem::exists(f)) { file = f; break; }
    const bool over = file && !g_latinFont.empty() && std::filesystem::exists(std::filesystem::u8path(g_latinFont));
    if (!ranges) ranges = io.Fonts->GetGlyphRangesDefault();
    if (!file) {
        ImFontConfig cfg;
        cfg.SizePixels = px;
        return io.Fonts->AddFontDefault(&cfg);
    }
    if (!over) return io.Fonts->AddFontFromFileTTF(file, px, nullptr, ranges);

    const ImWchar *repl = g_latinDigits ? lettersDigits : letters;
    // the base font's ranges without the replaced characters (kept alive with the atlas)
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
    io.Fonts->AddFontFromFileTTF(file, px, &cfg, ranges); // what the replacement lacks
    return f;
}

ImFont *addFontScaled(ImGuiIO &io, std::initializer_list<const char *> files, float size, float bake, const ImWchar *ranges)
{
    ImFont *f = addFontPx(io, files, size * bake, ranges);
    f->Scale = 1.f / bake; // laid out at `size`, rasterised at size * bake
    return f;
}

ImFont *addFont(ImGuiIO &io, std::initializer_list<const char *> files, float size, const ImWchar *ranges = nullptr)
{
    return addFontPx(io, files, size, ranges);
}

} // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int)
{
    if (!single::acquire()) return 0; // already running: its window came to the front
    // Real pixels on high-DPI screens (otherwise Windows stretches the window and blurs it); the UI scales itself.
    if (HMODULE u = GetModuleHandleW(L"user32.dll")) {
        using SetCtx = BOOL(WINAPI *)(HANDLE);
        if (auto f = reinterpret_cast<SetCtx>(reinterpret_cast<void *>(GetProcAddress(u, "SetProcessDpiAwarenessContext"))))
            f(reinterpret_cast<HANDLE>(-4)); // DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
    }

    // ---- arguments ----
    const std::string dir = dataDir();
    applog::open(dir);
    LOG("f1-race %s start, exe dir %s, data dir %s", F1RACE_VERSION, exeDir().c_str(), dir.c_str());
    app::App a(dir);
    auto &cfg = a.cfg;
    g_latinFont = cfg.latinFont;
    g_latinDigits = cfg.latinDigits;
    std::string replayFile;
    bool demo = false;
    int argc = 0;
    LPWSTR *wargv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::vector<std::string> args;
    for (int i = 0; i < argc; i++) args.push_back(std::filesystem::path(wargv[i]).string());
    LocalFree(wargv);
    {
        std::string line;
        for (int i = 1; i < argc; i++) line += " " + args[i];
        LOG("arguments:%s", line.empty() ? " (none)" : line.c_str());
    }
    for (int i = 1; i < argc; i++) {
        std::string arg = args[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? args[++i] : ""; };
        if (arg == "--port") cfg.port = uint16_t(std::atoi(next().c_str()));
        else if (arg == "--replay") replayFile = next();
        else if (arg == "--demo") demo = true;
        else if (arg == "--page") ui::setPage(std::atoi(next().c_str()));
    }

    // ---- telemetry source ----
    if (!replayFile.empty()) a.useReplay(replayFile);
    else if (demo || cfg.startSource == 1) a.useSim();
    else a.useLive();
    a.scanRecordings();

    // ---- SteamVR: read the headset first, it decides the panel texture's resolution ----
    vr_overlay::Overlays vr;
    std::string vrStatus;
    vr_overlay::HmdInfo hmd, layoutHmd; // what the headset has / the edges the panels hug
    layout::Fit fits[PANELS];
    auto refit = [&] {
        hmd = layout::applyOverrides(vr.hmd(), cfg.hmd);
        layoutHmd = layout::applyViewBox(hmd, cfg.hmd);
        layout::fitAll(layoutHmd, cfg.offset, fits);
    };
    auto startVr = [&](bool launch) {
        std::vector<vr_overlay::Panel> list;
        const panels::Rect rects[PANELS] = {panels::TOWER, panels::SECTORS, panels::PEDALS, panels::CAR, panels::STATUS, panels::WEATHER};
        const char *keys[PANELS] = {"lytton.f1race.tower", "lytton.f1race.sectors", "lytton.f1race.pedals", "lytton.f1race.car", "lytton.f1race.status", "lytton.f1race.weather"};
        const char *names[PANELS] = {"F1 Timing", "F1 Sectors", "F1 Pedals", "F1 Car", "F1 Status", "F1 Weather"};
        const float T = float(panels::TEX);
        for (int i = 0; i < PANELS; i++) {
            const auto &r = rects[i];
            list.push_back({keys[i], names[i], r.x / T, r.y / T, (r.x + r.w) / T, (r.y + r.h) / T, {}});
        }
        if (vr.start(std::move(list), &vrStatus, launch)) vrStatus = "SteamVR connected";
        vr.setOpacity(1.f); // opacity is baked into the texture: background and content separately
        refit();
        if (vr.running()) LOG("SteamVR: connected");
        else LOG("SteamVR: not connected (%s), panels use default headset values", vrStatus.c_str());
        if (cfg.hmd.renderW || cfg.hmd.renderH || cfg.hmd.fovH > 0 || cfg.hmd.fovV > 0)
            LOG("HMD overrides from f1-race.ini: render %u x %u, fov %.1f x %.1f", cfg.hmd.renderW, cfg.hmd.renderH,
                cfg.hmd.fovH, cfg.hmd.fovV);
        LOG("HMD used for layout: fov left %.1f right %.1f up %.1f down %.1f, both eyes %.1f .. %.1f, %.1f px/deg; "
            "panels hug %.1f .. %.1f x %.1f .. %.1f (view box %.1f x %.1f)",
            hmd.left, hmd.right, hmd.up, hmd.down, hmd.binoLeft, hmd.binoRight, hmd.ppd(), layoutHmd.binoLeft,
            layoutHmd.binoRight, layoutHmd.down, layoutHmd.up, cfg.hmd.viewH, cfg.hmd.viewV);
        for (int i = 0; i < PANELS; i++) {
            const auto p = layout::place(fits[i], cfg.offset[i]);
            vr.setPlacement(i, p);
            logPanel(i, fits[i], cfg.offset[i], p);
        }
    };
    startVr(false); // never starts SteamVR by itself: the VR page has a button for that
    auto lastVrTry = std::chrono::steady_clock::now();
    int vrTries = 0;
    layout::Offset savedOffset[PANELS];
    for (int i = 0; i < PANELS; i++) savedOffset[i] = cfg.offset[i];
    const float texScale = layout::textureScale(hmd);
    const UINT texSize = UINT(float(panels::TEX) * texScale);
    LOG("panel texture %u x %u (scale %.2f)", texSize, texSize, texScale);

    // ---- window + D3D ----
    WNDCLASSEXW wc{sizeof wc, CS_CLASSDC, wndProc, 0, 0, inst, LoadIconW(inst, MAKEINTRESOURCEW(1)), nullptr, nullptr, nullptr, L"f1race",
                   (HICON)LoadImageW(inst, MAKEINTRESOURCEW(1), IMAGE_ICON, 16, 16, 0)};
    RegisterClassExW(&wc);
    HWND hwnd = CreateWindowW(wc.lpszClassName, L"Lytton Telemetry Tool v" F1RACE_VERSION_W, WS_OVERLAPPEDWINDOW, 60, 40, 1680, 1000, nullptr,
                              nullptr, inst, nullptr);
    if (!createDevice(hwnd, texSize)) { destroyDevice(); return 1; }
    ShowWindow(hwnd, SW_SHOWMAXIMIZED);
    UpdateWindow(hwnd);

    // Panel context: renders the HMD texture, no input.
    ImGuiContext *panelCtx = ImGui::CreateContext();
    ImGui::SetCurrentContext(panelCtx);
    panels::Fonts pf;
    {
        ImGuiIO &io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = ImVec2(float(panels::TEX), float(panels::TEX));
        const auto faces = {"C:/Windows/Fonts/bahnschrift.ttf", "C:/Windows/Fonts/segoeuib.ttf", "C:/Windows/Fonts/arialbd.ttf"};
        // rasterise at the texture's scale; panels lay out in TEX units and the draw data is scaled up after
        pf.scale = texScale;
        pf.body = addFont(io, faces, 30.f * texScale);
        pf.small = addFont(io, faces, 20.f * texScale);
        pf.big = addFont(io, faces, 52.f * texScale);
        ImGui_ImplDX11_Init(g_dev, g_ctx);
    }

    // Desktop context: the main window, Chinese labels.
    g_deskCtx = ImGui::CreateContext();
    ImGui::SetCurrentContext(g_deskCtx);
    ui::Fonts uf;
    {
        ImGuiIO &io = ImGui::GetIO();
        io.IniFilename = nullptr;
        RECT rc;
        GetClientRect(hwnd, &rc);
        const float bake = std::clamp(uiScale(float(rc.right), float(rc.bottom)).s, 0.5f, 3.f);
        auto addFont = [bake](ImGuiIO &io, std::initializer_list<const char *> files, float size,
                              const ImWchar *ranges = nullptr) { return addFontScaled(io, files, size, bake, ranges); };
        const auto cjk = {"C:/Windows/Fonts/msyh.ttc", "C:/Windows/Fonts/simhei.ttf"};
        const auto cjkBold = {"C:/Windows/Fonts/msyhbd.ttc", "C:/Windows/Fonts/msyh.ttc", "C:/Windows/Fonts/simhei.ttf"};
        const auto nums = {"C:/Windows/Fonts/bahnschrift.ttf", "C:/Windows/Fonts/segoeui.ttf", "C:/Windows/Fonts/arial.ttf"};
        uf.text = addFont(io, cjk, 18.f, io.Fonts->GetGlyphRangesChineseFull());
        // the common set plus every character of the program's own text (ui_chars.inc), for the fonts that do not
        // load the full set
        static ImVector<ImWchar> uiRanges;
        {
            ImFontGlyphRangesBuilder b;
            b.AddRanges(io.Fonts->GetGlyphRangesChineseSimplifiedCommon());
            b.AddText(
#include "ui_chars.inc"
            );
            b.BuildRanges(&uiRanges);
        }
        uf.title = addFont(io, cjkBold, 24.f, uiRanges.Data);
        uf.num = addFont(io, nums, 19.f);
        uf.numMid = addFont(io, nums, 32.f);
        uf.numBig = addFont(io, nums, 64.f);
        uf.small = addFont(io, cjk, 15.f, uiRanges.Data);
        uf.numSmall = addFont(io, nums, 16.f);
        ui::setupStyle();
        ImGui_ImplWin32_Init(hwnd);
        ImGui_ImplDX11_Init(g_dev, g_ctx);
    }
    ui::VrEnv env;
    env.vr = &vr;
    env.hmd = &hmd;
    env.layoutHmd = &layoutHmd;
    env.fits = fits;
    env.texSize = texSize;
    env.texScale = texScale;
    env.panelTexture = g_panelSrv;
    env.status = &vrStatus;
    env.reconnect = [&] { startVr(false); };
    env.launch = [&] { startVr(true); };

    // ---- loop ----
    auto t0 = std::chrono::steady_clock::now(), last = t0, lastSave = t0;
    bool done = false;
    while (!done) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            if (msg.message == WM_QUIT) done = true;
        }
        if (done) break;
        if (g_resizeW) {
            if (g_backRtv) g_backRtv->Release();
            g_swap->ResizeBuffers(0, g_resizeW, g_resizeH, DXGI_FORMAT_UNKNOWN, 0);
            g_resizeW = g_resizeH = 0;
            createBackRtv();
        }
        auto now = std::chrono::steady_clock::now();
        const float dt = std::max(1e-4f, std::chrono::duration<float>(now - last).count());
        last = now;

        a.update();
        // SteamVR: notice when it quits, and keep trying to connect while it is not there (it may start after us)
        if (vr.running() && !vr.poll()) vrStatus = "SteamVR quit; waiting to reconnect";
        if (!vr.running() && now - lastVrTry > std::chrono::seconds(5)) {
            lastVrTry = now;
            vrTries++;
            applog::hold(); // a failed retry is not logged again; a successful one is, in full
            startVr(false);
            applog::release(vr.running());
            if (vr.running()) {
                LOG("SteamVR: connected on retry %d", vrTries);
                if (layout::textureScale(hmd) != texScale)
                    LOG("panel texture was sized for the default headset (scale %.2f, this one wants %.2f); "
                        "restart the program for full sharpness", texScale, layout::textureScale(hmd));
                vrTries = 0;
            }
        }
        if (a.dirty && now - lastSave > std::chrono::seconds(1)) {
            for (int i = 0; i < PANELS; i++)
                if (!sameOffset(cfg.offset[i], savedOffset[i])) {
                    logPanel(i, fits[i], cfg.offset[i], layout::place(fits[i], cfg.offset[i]));
                    savedOffset[i] = cfg.offset[i];
                }
            a.save();
            lastSave = now;
        }

        timing::Snapshot snap = a.race.snapshot();

        // HMD texture
        ImGui::SetCurrentContext(panelCtx);
        ImGui_ImplDX11_NewFrame();
        ImGui::GetIO().DeltaTime = dt;
        ImGui::NewFrame();
        panels::draw(snap, pf, panels::GapMode(cfg.gapMode));
        ImGui::Render();
        panels::applyOpacity(ImGui::GetDrawData(), cfg.bgOpacity, cfg.textOpacity);
        if (ImDrawData *dd = ImGui::GetDrawData(); texScale != 1.f) { // layout units -> texture pixels
            for (ImDrawList *l : dd->CmdLists)
                for (ImDrawVert &v : l->VtxBuffer) { v.pos.x *= texScale; v.pos.y *= texScale; }
            dd->ScaleClipRects(ImVec2(texScale, texScale));
            dd->DisplaySize = ImVec2(float(texSize), float(texSize));
        }
        const float clear[4] = {0, 0, 0, 0};
        g_ctx->OMSetRenderTargets(1, &g_panelRtv, nullptr);
        g_ctx->ClearRenderTargetView(g_panelRtv, clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        vr.submit(g_panelTex);

        // Desktop window
        ImGui::SetCurrentContext(g_deskCtx);
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        // lay out in the design space: mouse positions into it, display size fixed
        static UiScale us;
        {
            ImGuiIO &io = ImGui::GetIO();
            if (io.DisplaySize.x >= 1 && io.DisplaySize.y >= 1) us = uiScale(io.DisplaySize.x, io.DisplaySize.y);
            for (ImGuiInputEvent &e : ImGui::GetCurrentContext()->InputEventsQueue)
                if (e.Type == ImGuiInputEventType_MousePos && e.MousePos.PosX > -FLT_MAX / 2) {
                    e.MousePos.PosX = (e.MousePos.PosX - us.offX) / us.s;
                    e.MousePos.PosY = (e.MousePos.PosY - us.offY) / us.s;
                }
            io.DisplaySize = ImVec2(UI_W, UI_H);
        }
        ImGui::NewFrame();
        ui::draw(a, uf, env);

        ImGui::Render();
        if (ImDrawData *dd = ImGui::GetDrawData()) { // design space -> window pixels
            for (ImDrawList *l : dd->CmdLists) {
                for (ImDrawVert &v : l->VtxBuffer) {
                    v.pos.x = v.pos.x * us.s + us.offX;
                    v.pos.y = v.pos.y * us.s + us.offY;
                }
                for (ImDrawCmd &c : l->CmdBuffer)
                    c.ClipRect = ImVec4(c.ClipRect.x * us.s + us.offX, c.ClipRect.y * us.s + us.offY,
                                        c.ClipRect.z * us.s + us.offX, c.ClipRect.w * us.s + us.offY);
            }
            dd->DisplayPos = ImVec2(0, 0);
            dd->DisplaySize = ImVec2(UI_W * us.s + 2 * us.offX, UI_H * us.s + 2 * us.offY);
        }
        const float bg[4] = {13 / 255.f, 15 / 255.f, 19 / 255.f, 1.f}; // same as the UI background, for the margins
        g_ctx->OMSetRenderTargets(1, &g_backRtv, nullptr);
        g_ctx->ClearRenderTargetView(g_backRtv, bg);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        g_swap->Present(1, 0);
    }

    // ---- shutdown ----
    LOG("exit");
    a.stopRecording();
    vr.stop();
    ImGui::SetCurrentContext(g_deskCtx);
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext(g_deskCtx);
    ImGui::SetCurrentContext(panelCtx);
    ImGui_ImplDX11_Shutdown();
    ImGui::DestroyContext(panelCtx);
    destroyDevice();
    DestroyWindow(hwnd);
    applog::close();
    return 0;
}
