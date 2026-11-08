// Where the HMD panels go, worked out from the headset's real optics.
//
// Every panel is anchored to an edge of the visible view (the region both eyes see horizontally — wide headsets
// like the Pimax Crystal Super see their outer edges with one eye only — and the vertical field of view), pulled
// in by a small inset. A different headset therefore puts the panels at its own edges. The result is a head-locked
// placement 1 m away; the user's saved offsets are added on top. The panel texture's resolution is chosen from the
// headset's render pixels per degree so text stays sharp.
#pragma once

#include "vr_overlay.hpp"

namespace layout {

constexpr int PANELS = 6; // 0 tower, 1 sectors, 2 pedals, 3 car (tyres), 4 status (wear, damage, failures),
                          // 5 weather forecast
constexpr float DIST = 1.0f; // metres from the eyes

enum class H { Left, Centre, Right };
enum class V { Top, Middle, Bottom };

struct Anchor {
    H h;
    V v;
    float insetDeg;  // distance from the edge(s) it is anchored to
    float widthDeg;  // angular width
    float aspect;    // height / width of the panel's texture region
    int below = -1;  // >= 0: stacked under that panel (insetDeg = the gap), its left (H::Left) or right edge
};

// tower: top-left corner  ·  sectors: top-right corner  ·  pedals: bottom edge, centre  ·  car: under the tower  ·  status: under the sectors  ·  weather: top centre
extern const Anchor BASE[PANELS];

struct Offset {
    float daz = 0, del = 0;     // degrees: moves the panel across the view (+ right / up), still facing the eyes
    float dz = 0;               // metres along the line of sight (negative = farther)
    float dyaw = 0, dpitch = 0; // degrees, extra turn / tilt on top of facing the eyes
    float scale = 1;              // multiplies the width
    bool visible = true;
    // Fixed in the cockpit instead of following the head: a point in SteamVR's seated space (origin = the head at
    // the last "reset seated position", metres: +x right, +y up, -z forward). The panel faces that origin;
    // dyaw / dpitch / scale still apply, daz / del / dz do not.
    bool cockpit = false;
    float cx = 0, cy = -0.35f, cz = -0.6f;
};

struct Fit {
    float azDeg, elDeg, widthDeg, heightDeg; // panel centre and size in the view
};

// Headset values the user can force in f1-race.ini (0 = use what SteamVR reports).
struct Overrides {
    uint32_t renderW = 0, renderH = 0;
    float fovH = 0, fovV = 0;
    // The part of the view the panels hug (degrees, centred; 0 = the whole field of view). Default: what was
    // tuned in the Pimax Crystal on 2026-11-06 - its optics reach much further, but that is where text is
    // still readable without moving the eyes far.
    float viewH = 58.4f, viewV = 22.6f;
};

vr_overlay::HmdInfo applyOverrides(vr_overlay::HmdInfo h, const Overrides &o);
// Narrows the edges the panels anchor to (binocular range, up, down) to the view box.
vr_overlay::HmdInfo applyViewBox(vr_overlay::HmdInfo h, const Overrides &o);
Fit fit(int panel, const vr_overlay::HmdInfo &h);
// All panels; a stacked panel follows where its parent really is (the parent's offset and scale included).
void fitAll(const vr_overlay::HmdInfo &h, const Offset offsets[PANELS], Fit out[PANELS]);
vr_overlay::Placement place(const Fit &f, const Offset &o);
// A panel's offset before the user changes it (the pedals start fixed in the cockpit).
Offset defaultOffset(int panel);
// 1.0 .. 2.0 in 0.25 steps
float textureScale(const vr_overlay::HmdInfo &h);

} // namespace layout
