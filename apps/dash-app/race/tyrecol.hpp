// Tyre carcass (inner) temperature as a colour, by compound: blue when cold, green in the compound's working window,
// then yellow to red when overheating, as a smooth hue. Shared by the desktop car card and the HMD car panel.
//
// The windows are the usual community figures for the F1 games' inner temperatures (the slicks' windows rise with
// hardness, as in Pirelli's published ranges; inters and wets work much cooler). They are a guide, not game data:
// the telemetry has no working-window field.
#pragma once

#include <imgui.h>

#include <algorithm>
#include <cstdint>

namespace tyrecol {

struct Window {
    float lo, hi; // °C
};

// visualTyreCompound (CarStatus): 16 soft, 17 medium, 18 hard, 7 inter, 8 wet; anything else = medium.
inline Window window(uint8_t visual)
{
    switch (visual) {
    case 16: return {85, 105};
    case 18: return {95, 115};
    case 7: return {65, 85};
    case 8: return {50, 70};
    default: return {90, 110};
    }
}

inline ImU32 hsv(float h, float s, float v)
{
    float r, g, b;
    ImGui::ColorConvertHSVtoRGB(h, s, v, r, g, b);
    return ImGui::ColorConvertFloat4ToU32(ImVec4(r, g, b, 1.f));
}

// Hue: blue (0.6) 20 °C under the window -> green (0.33) inside it -> red (0) 20 °C over it.
inline ImU32 temp(float t, uint8_t visual)
{
    const Window w = window(visual);
    float h = 1.f / 3.f;
    if (t < w.lo) h += (0.6f - h) * std::clamp((w.lo - t) / 20.f, 0.f, 1.f);
    else if (t > w.hi) h *= 1.f - std::clamp((t - w.hi) / 20.f, 0.f, 1.f);
    return hsv(h, 0.75f, 1.f);
}

// Brake disc temperature: blue (cold, 200 °C and under) -> green (working, 400-900 °C) -> red (1100 °C and over).
inline ImU32 brake(float t)
{
    float h = 1.f / 3.f;
    if (t < 400) h += (0.6f - h) * std::clamp((400 - t) / 200.f, 0.f, 1.f);
    else if (t > 900) h *= 1.f - std::clamp((t - 900) / 200.f, 0.f, 1.f);
    return hsv(h, 0.75f, 1.f);
}

// Wear: green (new) through yellow to red at 75 % and more.
inline ImU32 wear(float pct) { return hsv(std::clamp(1.f - pct / 75.f, 0.f, 1.f) / 3.f, 0.75f, 1.f); }

} // namespace tyrecol
