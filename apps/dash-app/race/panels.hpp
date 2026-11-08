// The HMD panels, drawn with ImGui's draw list into one shared texture. Each panel owns a fixed rectangle of the
// texture; the same rectangles become the SteamVR overlays' texture bounds.
#pragma once

#include "race_state.hpp"

struct ImFont;
struct ImDrawData;
struct ImDrawList;
struct ImVec2;

namespace panels {

constexpr int TEX = 1024; // panel layout space is TEX x TEX; the real texture is TEX * Fonts::scale

struct Rect {
    float x, y, w, h;
};

// Panel rectangles in texture pixels.
constexpr Rect TOWER{0, 0, 440, 360}; // compact tower: 6 rows
constexpr Rect SECTORS{448, 0, 576, 300};
constexpr Rect PEDALS{0, 904, 1024, 120}; // full width: brake % | brake cells | rpm | throttle cells | throttle %
constexpr Rect CAR{0, 368, 400, 280};      // under the tower: tyres on a top view of the car, coloured by damage
constexpr Rect STATUS{448, 308, 576, 200}; // under the sectors: power unit wear, damaged parts, failures
constexpr Rect WEATHER{448, 516, 576, 124}; // top centre: this session's forecast

struct Fonts {
    ImFont *small = nullptr;  // labels
    ImFont *body = nullptr;   // rows
    ImFont *big = nullptr;    // lap time
    float scale = 1;          // texture scale: fonts are rasterised at size * scale, layout stays in TEX units
};

// What the tower's gap column shows.
enum class GapMode { Relative = 0, Interval = 1, Leader = 2 }; // to you (default) / to the car ahead / to the leader

// Draws all panels into the current ImGui frame (display size TEX x TEX).
void draw(const timing::Snapshot &s, const Fonts &f, GapMode gaps);

// After ImGui::Render(): scale the alpha of the panel backgrounds and of everything drawn on them separately.
void applyOpacity(ImDrawData *dd, float background, float content);

// A weather symbol (0 clear .. 5 storm) centred on c, sz across; the desktop dashboard uses it too.
void weatherSymbol(ImDrawList *dl, const ImVec2 &c, float sz, uint8_t weather);

// Plausible moving data for working on the layout without the game.
timing::Snapshot demo(double seconds);

} // namespace panels
