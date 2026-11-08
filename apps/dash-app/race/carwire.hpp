// The F1 car top view (nose up) drawn as a wireframe: Track N Race's car drawing (NoGoat, GPL-3.0;
// third_party/track-n-race/car, converted by scripts/gen_car_wire.py). Each damage zone and tyre can be filled with
// its own colour; the outlines go on top. Used by the desktop car card and the HMD car panel.
#pragma once

#include <imgui.h>

namespace carwire {

enum Part : unsigned char { BODY, COCKPIT, SUSPENSION, FW_L, FW_R, FLOOR, DIFF, SP_L, SP_R, RW, T_FL, T_FR, T_RL, T_RR, PARTS };

// Where the car goes: screen = origin + drawing units * k.
struct Placement {
    ImVec2 origin{0, 0};
    float k = 1;
    ImVec2 at(float x, float y) const { return ImVec2(origin.x + x * k, origin.y + y * k); }
};
// The car as large as fits in the box, centred.
Placement fit(ImVec2 topLeft, ImVec2 size);
float aspect(); // width / height

struct Colours {
    ImU32 fill[PARTS] = {}; // per part, 0 = the background
    ImU32 bg = IM_COL32(17, 20, 31, 255), line = IM_COL32(138, 147, 196, 255), tube = IM_COL32(74, 81, 117, 255);
};
void draw(ImDrawList *dl, const Placement &p, const Colours &c);

// Optional: draw the car as one image instead of the polylines. The Qt program renders Track N Race's original SVG
// with these colours at `w` x `h` (the drawing's size on screen, in layout units) and returns a texture for the
// current ImGui context; null = fall back to the polylines.
using ImageFn = ImTextureID (*)(const Colours &c, float w, float h);
void setImageRenderer(ImageFn fn);

// The screen box of a part's filled shape (e.g. a tyre), for numbers and markings on it.
void box(Part part, const Placement &p, ImVec2 &a, ImVec2 &b);

} // namespace carwire
