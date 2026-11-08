#include "layout.hpp"

#include "panels.hpp"

#include <algorithm>
#include <cmath>

namespace layout {

namespace {
constexpr float D2R = 3.14159265f / 180.f;
float aspectOf(const panels::Rect &r) { return r.h / r.w; }

float heightDeg(float widthDeg, float aspect)
{
    const float wM = 2 * DIST * std::tan(widthDeg / 2 * D2R);
    return 2 * std::atan(wM * aspect / 2 / DIST) / D2R;
}
} // namespace

const Anchor BASE[PANELS] = {
    {H::Left, V::Top, 0.f, 10.f, aspectOf(panels::TOWER)},
    {H::Right, V::Top, 0.f, 17.f, aspectOf(panels::SECTORS)},
    {H::Centre, V::Bottom, 0.f, 14.2f, aspectOf(panels::PEDALS)},
    {H::Left, V::Top, 0.6f, 12.f, aspectOf(panels::CAR), 0},
    {H::Right, V::Top, 0.6f, 17.f, aspectOf(panels::STATUS), 1},
    {H::Centre, V::Top, 0.f, 20.f, aspectOf(panels::WEATHER)},
};

vr_overlay::HmdInfo applyOverrides(vr_overlay::HmdInfo h, const Overrides &o)
{
    if (o.renderW) h.renderW = o.renderW;
    if (o.renderH) h.renderH = o.renderH;
    if (o.fovH > 0) {
        h.left = -o.fovH / 2;
        h.right = o.fovH / 2;
        // without SteamVR's per-eye data, assume the outer 10° each side is seen by one eye only
        h.binoLeft = h.left + std::min(10.f, o.fovH / 6);
        h.binoRight = -h.binoLeft;
        h.eyeFovH = h.binoRight - h.left;
    }
    if (o.fovV > 0) {
        h.up = o.fovV / 2;
        h.down = -o.fovV / 2;
    }
    return h;
}

namespace {
Fit stack(int i, const Fit &parent)
{
    const Anchor &a = BASE[i];
    Fit f{0, 0, a.widthDeg, heightDeg(a.widthDeg, a.aspect)};
    f.azDeg = a.h == H::Right ? parent.azDeg + parent.widthDeg / 2 - f.widthDeg / 2
              : a.h == H::Left ? parent.azDeg - parent.widthDeg / 2 + f.widthDeg / 2
                               : parent.azDeg;
    f.elDeg = parent.elDeg - parent.heightDeg / 2 - a.insetDeg - f.heightDeg / 2;
    return f;
}
} // namespace

void fitAll(const vr_overlay::HmdInfo &h, const Offset offsets[PANELS], Fit out[PANELS])
{
    for (int i = 0; i < PANELS; i++) {
        const int p = BASE[i].below;
        if (p < 0) { out[i] = fit(i, h); continue; }
        Fit parent = out[p]; // parents come first in BASE
        const Offset &o = offsets[p];
        if (!o.cockpit) {
            parent.azDeg += o.daz;
            parent.elDeg += o.del;
            parent.widthDeg *= o.scale;
            parent.heightDeg *= o.scale;
        }
        out[i] = stack(i, parent);
    }
}

vr_overlay::HmdInfo applyViewBox(vr_overlay::HmdInfo h, const Overrides &o)
{
    if (o.viewH > 0) {
        h.binoLeft = std::max(h.binoLeft, -o.viewH / 2);
        h.binoRight = std::min(h.binoRight, o.viewH / 2);
    }
    if (o.viewV > 0) {
        h.up = std::min(h.up, o.viewV / 2);
        h.down = std::max(h.down, -o.viewV / 2);
    }
    return h;
}

Fit fit(int i, const vr_overlay::HmdInfo &h)
{
    const Anchor &a = BASE[i];
    if (a.below >= 0) return stack(i, fit(a.below, h));
    Fit f{0, 0, a.widthDeg, heightDeg(a.widthDeg, a.aspect)};
    const float halfW = f.widthDeg / 2, halfH = f.heightDeg / 2;
    switch (a.h) {
    case H::Left: f.azDeg = h.binoLeft + a.insetDeg + halfW; break;
    case H::Centre: f.azDeg = (h.binoLeft + h.binoRight) / 2; break;
    case H::Right: f.azDeg = h.binoRight - a.insetDeg - halfW; break;
    }
    switch (a.v) {
    case V::Top: f.elDeg = h.up - a.insetDeg - halfH; break;
    case V::Middle: f.elDeg = (h.up + h.down) / 2; break;
    case V::Bottom: f.elDeg = h.down + a.insetDeg + halfH; break;
    }
    return f;
}

Offset defaultOffset(int i)
{
    Offset o;
    if (i == 2) { // straight ahead, where the view box puts it: 1 m away, 10.5 deg down
        o.cockpit = true;
        o.cx = 0;
        o.cy = -0.1817f;
        o.cz = -0.9834f;
    }
    return o;
}

vr_overlay::Placement place(const Fit &f, const Offset &o)
{
    if (o.cockpit) {
        vr_overlay::Placement p;
        p.x = o.cx;
        p.y = o.cy;
        p.z = o.cz;
        const float flat = std::sqrt(o.cx * o.cx + o.cz * o.cz);
        p.yawDeg = std::atan2(-o.cx, -o.cz) / D2R + o.dyaw; // face the seated origin (the driver's head)
        p.pitchDeg = -std::atan2(o.cy, std::max(flat, 0.01f)) / D2R + o.dpitch; // below the eyes: top tilted back
        p.widthM = 2 * DIST * std::tan(f.widthDeg / 2 * D2R) * o.scale;
        p.visible = o.visible;
        p.headLocked = false;
        return p;
    }
    const float azDeg = f.azDeg + o.daz, elDeg = std::clamp(f.elDeg + o.del, -89.f, 89.f);
    const float az = azDeg * D2R, el = elDeg * D2R;
    const float d = std::max(0.2f, DIST - o.dz); // dz < 0 pushes it farther
    vr_overlay::Placement p;
    p.x = d * std::cos(el) * std::sin(az);
    p.y = d * std::sin(el);
    p.z = -d * std::cos(el) * std::cos(az);
    p.yawDeg = -azDeg + o.dyaw; // face the eyes
    p.pitchDeg = -elDeg + o.dpitch;
    p.widthM = 2 * DIST * std::tan(f.widthDeg / 2 * D2R) * o.scale;
    p.visible = o.visible;
    p.headLocked = true;
    return p;
}

float textureScale(const vr_overlay::HmdInfo &h)
{
    const float ppd = h.ppd();
    if (ppd <= 0) return 1;
    const panels::Rect rects[PANELS] = {panels::TOWER, panels::SECTORS, panels::PEDALS, panels::CAR, panels::STATUS, panels::WEATHER};
    float minPanelPpd = 1e9f;
    for (int i = 0; i < PANELS; i++) minPanelPpd = std::min(minPanelPpd, rects[i].w / BASE[i].widthDeg);
    const float want = ppd * 1.25f / minPanelPpd; // a little oversampling for crisp text
    return std::clamp(std::ceil(want * 4) / 4, 1.f, 2.f);
}

} // namespace layout
