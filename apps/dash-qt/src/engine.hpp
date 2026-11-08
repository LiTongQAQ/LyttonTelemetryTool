// The part of the program that does not depend on the desktop UI: the telemetry source (app::App), the HMD panel
// texture (ImGui into an offscreen D3D11 texture, no window) and the SteamVR overlays. The Qt window calls tick()
// from a timer (source, SteamVR reconnects, settings); the HMD panels render and go to SteamVR on their own thread
// at 90 Hz, so a hidden or busy desktop window never slows them down. The panels are as in the ImGui program.
#pragma once

#include "app.hpp"
#include "layout.hpp"
#include "panels.hpp"
#include "vr_overlay.hpp"

#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>
#include <initializer_list>
#include <map>
#include <string>
#include <vector>

struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11Texture2D;
struct ID3D11RenderTargetView;
struct ID3D11ShaderResourceView;
struct ImGuiContext;
struct ImFont;
struct ImGuiIO;
typedef unsigned short ImWchar16;

namespace engine {

std::string exeDir();
// An ImGui font: the first existing file at `px` pixels, the Latin override font of the settings merged in
// (setLatinFont), `ranges` = null for the default set.
ImFont *addFont(ImGuiIO &io, std::initializer_list<const char *> files, float px, const ImWchar16 *ranges = nullptr);
std::string dataDir(); // next to the exe when writable, else %LOCALAPPDATA%\LyttonTelemetryTool

class Engine {
public:
    explicit Engine(app::App &a);
    ~Engine();

    bool init();          // D3D11 device and panel texture, connects to a running SteamVR, starts the HMD thread
    void tick(float dt);  // GUI thread: App::update, SteamVR reconnects, settings save, the snapshot for the desktop
    double hmdFps() const { return hmdFps_; } // HMD panel frames per second, measured
    void startVr(bool launch); // launch = start SteamVR if it is not running
    void refit();          // panel placement after an offset / headset override change
    bool readPanel(std::vector<uint8_t> &rgba, unsigned &size); // the HMD texture, for the preview on the VR page
    bool inPanelFrame() const; // the HMD panels' ImGui context is the current one
    // an RGBA (straight alpha) image as a texture for the HMD panels, one per `slot`, updated in place
    void *uploadTexture(const std::string &slot, const unsigned char *rgba, int w, int h);

    app::App &a;
    timing::Snapshot snap; // of the last tick

    vr_overlay::Overlays vr;
    std::string vrStatus;
    vr_overlay::HmdInfo hmd, layoutHmd;
    layout::Fit fits[layout::PANELS];
    unsigned texSize = 0;
    float texScale = 1;

private:
    void hmdLoop();
    void renderPanels(const timing::Snapshot &s, float dt);
    std::thread hmdThread_;
    std::atomic<bool> hmdRun_{false};
    std::atomic<double> hmdFps_{0};
    std::mutex d3d_; // the immediate context: HMD thread renders, GUI thread reads the preview back
    ID3D11Device *dev_ = nullptr;
    ID3D11DeviceContext *ctx_ = nullptr;
    ID3D11Texture2D *tex_ = nullptr;
    ID3D11RenderTargetView *rtv_ = nullptr;
    ID3D11Texture2D *staging_ = nullptr;
    struct Upload {
        ID3D11Texture2D *tex = nullptr;
        ID3D11ShaderResourceView *srv = nullptr;
        int w = 0, h = 0;
    };
    std::map<std::string, Upload> uploads_;
    ImGuiContext *imgui_ = nullptr;
    panels::Fonts pf_;
    std::chrono::steady_clock::time_point lastVrTry_, lastSave_;
    int vrTries_ = 0;
    layout::Offset savedOffset_[layout::PANELS];
};

} // namespace engine
