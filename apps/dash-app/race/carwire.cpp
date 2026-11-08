#include "carwire.hpp"

#include <algorithm>
#include <vector>

namespace carwire {

namespace {
enum Role : unsigned char { SOLID, WIRE, DETAIL, TUBE };
struct WireShape {
    Part part;
    Role role;
    bool closed;
    int first, count; // points in WIRE_PTS (pairs)
};
#include "carwire.inc"

ImU32 withAlpha(ImU32 c, float a)
{
    const unsigned al = unsigned(float((c >> IM_COL32_A_SHIFT) & 0xFF) * a);
    return (c & ~IM_COL32_A_MASK) | (al << IM_COL32_A_SHIFT);
}
ImageFn g_image = nullptr;
} // namespace

void setImageRenderer(ImageFn fn) { g_image = fn; }

float aspect() { return WIRE_W / WIRE_H; }

Placement fit(ImVec2 topLeft, ImVec2 size)
{
    Placement p;
    p.k = std::min(size.x / WIRE_W, size.y / WIRE_H);
    p.origin = ImVec2(topLeft.x + (size.x - WIRE_W * p.k) / 2, topLeft.y + (size.y - WIRE_H * p.k) / 2);
    return p;
}

void draw(ImDrawList *dl, const Placement &p, const Colours &c)
{
    if (g_image)
        if (ImTextureID tex = g_image(c, WIRE_W * p.k, WIRE_H * p.k)) {
            dl->AddImage(tex, p.origin, p.at(WIRE_W, WIRE_H));
            return;
        }
    // line widths as drawn in the original (2.4 / 1.4 / 1.8 units), never thinner than a pixel
    const float wWire = std::max(1.f, 2.4f * p.k * 1.5f), wDetail = std::max(0.8f, 1.4f * p.k * 1.5f);
    const float wTube = std::max(0.8f, 1.8f * p.k * 1.5f);
    std::vector<ImVec2> pts;
    for (const auto &s : WIRE_SHAPES) { // in the drawing's order: later shapes cover earlier lines
        pts.clear();
        for (int i = 0; i < s.count; i++) pts.push_back(p.at(WIRE_PTS[(s.first + i) * 2], WIRE_PTS[(s.first + i) * 2 + 1]));
        if (pts.size() < 2) continue;
        switch (s.role) {
        case SOLID:
            if (pts.size() >= 3) {
                dl->AddConcavePolyFilled(pts.data(), int(pts.size()), c.bg);
                if (c.fill[s.part]) dl->AddConcavePolyFilled(pts.data(), int(pts.size()), c.fill[s.part]);
            }
            dl->AddPolyline(pts.data(), int(pts.size()), c.line, s.closed ? ImDrawFlags_Closed : 0, wWire);
            break;
        case WIRE:
            dl->AddPolyline(pts.data(), int(pts.size()), c.line, s.closed ? ImDrawFlags_Closed : 0, wWire);
            break;
        case DETAIL:
            dl->AddPolyline(pts.data(), int(pts.size()), withAlpha(c.line, 0.6f), s.closed ? ImDrawFlags_Closed : 0, wDetail);
            break;
        case TUBE:
            dl->AddPolyline(pts.data(), int(pts.size()), c.tube, s.closed ? ImDrawFlags_Closed : 0, wTube);
            break;
        }
    }
}

void box(Part part, const Placement &p, ImVec2 &a, ImVec2 &b)
{
    float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
    for (const auto &s : WIRE_SHAPES) {
        if (s.part != part || s.role != SOLID) continue;
        for (int i = 0; i < s.count; i++) {
            const float x = WIRE_PTS[(s.first + i) * 2], y = WIRE_PTS[(s.first + i) * 2 + 1];
            x0 = std::min(x0, x); x1 = std::max(x1, x);
            y0 = std::min(y0, y); y1 = std::max(y1, y);
        }
        break; // the first solid shape is the part's outline
    }
    a = p.at(x0, y0);
    b = p.at(x1, y1);
}

} // namespace carwire
