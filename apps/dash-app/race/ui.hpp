// The desktop window: sidebar navigation and the pages (dashboard, lap analysis, capture, replay, forwarding,
// HMD panels, settings). Drawn with ImGui into the desktop context every frame.
#pragma once

#include "app.hpp"
#include "layout.hpp"
#include "vr_overlay.hpp"

#include <functional>

struct ImFont;

namespace ui {

struct Fonts {
    ImFont *text = nullptr;   // UI text, Chinese
    ImFont *title = nullptr;  // page titles
    ImFont *num = nullptr;    // numbers in tables and cards
    ImFont *numMid = nullptr; // card values
    ImFont *numBig = nullptr; // speed, gear
    ImFont *small = nullptr;    // dense cards (tyres and car condition), Chinese (common characters)
    ImFont *numSmall = nullptr; // numbers in dense cards
};

// What the HMD page needs from main(): the headset, the overlays and the panel texture.
struct VrEnv {
    vr_overlay::Overlays *vr = nullptr;
    vr_overlay::HmdInfo *hmd = nullptr;
    vr_overlay::HmdInfo *layoutHmd = nullptr; // edges the panels hug (view box applied)
    layout::Fit *fits = nullptr;
    unsigned texSize = 0;
    float texScale = 1;
    void *panelTexture = nullptr; // ImTextureID of the panel texture
    std::string *status = nullptr;
    std::function<void()> reconnect; // connect to a running SteamVR
    std::function<void()> launch;    // start SteamVR and connect
};

void setupStyle();
void setPage(int page); // 0 dashboard .. 6 settings
void draw(app::App &a, const Fonts &f, VrEnv &vr);

// Pieces for the Qt window: the same drawing, one piece per ImGui canvas (call bind() first).
void bind(const Fonts &f);
int page();
bool mapFull();
void setMapFull(bool on);
bool transportShown(app::App &a);
void drawSidebar(app::App &a);
void drawTopBar(app::App &a);
void drawTransport(app::App &a);
void drawPage(app::App &a, VrEnv &vr, int page);
bool drawEmpty(app::App &a, const timing::Snapshot &s);
void drawStats(const timing::Snapshot &s, float w);
void drawWeather(const timing::Snapshot &s, float w, float h); // the forecast strip (full-screen map)
void drawCar(const timing::Snapshot &s, float w, float h);
void drawRecent(app::App &a, float w, float h);
void drawMap(app::App &a, const timing::Snapshot &s, float w, float h);
void drawCarStatus(const timing::Snapshot &s, float w, float h);
void drawStandings(app::App &a, const timing::Snapshot &s, float w, float h);

} // namespace ui
