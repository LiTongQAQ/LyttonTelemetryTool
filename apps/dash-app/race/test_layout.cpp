// Layout maths without a headset: edge anchoring, placement geometry, texture scale.
#include "layout.hpp"

#include <cmath>
#include <cstdio>

static int failures = 0;
#define CHECK(cond)                                                                                                    \
    do {                                                                                                               \
        if (!(cond)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); failures++; }                       \
    } while (0)
static bool near(float a, float b, float eps = 0.01f) { return std::fabs(a - b) < eps; }

int main()
{
    using namespace layout;

    // A wide headset (roughly a Pimax Crystal Super UW class).
    vr_overlay::HmdInfo wide;
    wide.valid = true;
    wide.renderW = 4300;
    wide.left = -70; wide.right = 70; wide.binoLeft = -58; wide.binoRight = 58; wide.up = 48; wide.down = -48;
    wide.eyeFovH = 128;

    // Each panel hugs its edge of the binocular region, inset by BASE[i].insetDeg.
    Fit tower = fit(0, wide), sectors = fit(1, wide), pedals = fit(2, wide);
    CHECK(near(tower.azDeg - tower.widthDeg / 2, wide.binoLeft + BASE[0].insetDeg));
    CHECK(near(tower.elDeg + tower.heightDeg / 2, wide.up - BASE[0].insetDeg)); // top-left corner
    CHECK(near(sectors.azDeg + sectors.widthDeg / 2, wide.binoRight - BASE[1].insetDeg));
    CHECK(near(sectors.elDeg + sectors.heightDeg / 2, wide.up - BASE[1].insetDeg));
    CHECK(near(pedals.azDeg, 0));
    CHECK(near(pedals.elDeg - pedals.heightDeg / 2, wide.down + BASE[2].insetDeg));
    std::printf("wide: tower az %.1f, sectors az %.1f el %.1f, pedals el %.1f\n", tower.azDeg, sectors.azDeg,
                sectors.elDeg, pedals.elDeg);

    // A narrower headset moves them inward with its edges.
    vr_overlay::HmdInfo narrow = wide;
    narrow.binoLeft = -40; narrow.binoRight = 40; narrow.up = 40; narrow.down = -40;
    CHECK(fit(1, narrow).azDeg < sectors.azDeg && fit(1, narrow).elDeg < sectors.elDeg);
    CHECK(fit(0, narrow).azDeg > tower.azDeg && fit(0, narrow).elDeg < tower.elDeg);
    CHECK(fit(2, narrow).elDeg > pedals.elDeg);

    // Placement: on a 1 m sphere, facing the eyes, head-locked.
    auto p = place(sectors, Offset{});
    CHECK(p.headLocked);
    CHECK(near(std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z), DIST));
    CHECK(p.x > 0 && p.y > 0 && p.z < 0);
    CHECK(near(p.yawDeg, -sectors.azDeg) && near(p.pitchDeg, -sectors.elDeg));
    CHECK(near(p.widthM, 2 * std::tan(BASE[1].widthDeg / 2 * 3.14159265f / 180.f), 0.001f));

    // Offsets add on top.
    Offset o;
    o.dyaw = 5; o.scale = 1.5f; o.visible = false;
    auto q = place(sectors, o);
    CHECK(near(q.x, p.x) && near(q.yawDeg, p.yawDeg + 5) && near(q.widthM, p.widthM * 1.5f) && !q.visible);

    // Moving across the view keeps the panel on the sphere and facing the eyes.
    Offset m;
    m.del = -30; m.daz = -10;
    auto r = place(sectors, m);
    CHECK(near(std::sqrt(r.x * r.x + r.y * r.y + r.z * r.z), DIST));
    CHECK(r.y < p.y && near(r.pitchDeg, -(sectors.elDeg - 30)) && near(r.yawDeg, -(sectors.azDeg - 10)));
    m.dz = -0.5f;
    auto s = place(sectors, m);
    CHECK(near(std::sqrt(s.x * s.x + s.y * s.y + s.z * s.z), DIST + 0.5f));

    // Fixed in the cockpit: seated space, at the given point, facing the origin.
    Offset c;
    c.cockpit = true; c.cx = 0; c.cy = -0.35f; c.cz = -0.6f;
    auto k = place(fit(2, wide), c);
    CHECK(!k.headLocked && near(k.y, -0.35f) && near(k.z, -0.6f) && near(k.yawDeg, 0));
    CHECK(near(k.pitchDeg, std::atan2(0.35f, 0.6f) * 180.f / 3.14159265f, 0.01f));
    c.cx = 0.3f;
    CHECK(place(fit(2, wide), c).yawDeg < 0); // to the right: turned left, towards the driver

    // The view box: on the Pimax Crystal (as SteamVR reports it) the panels land where they were tuned by hand.
    vr_overlay::HmdInfo pimax;
    pimax.valid = true;
    pimax.renderW = 3212; pimax.eyeFovH = 113.5f;
    pimax.left = -69.1f; pimax.right = 69.1f; pimax.binoLeft = -44.4f; pimax.binoRight = 44.4f;
    pimax.up = 59.8f; pimax.down = -59.8f;
    const auto box = applyViewBox(pimax, Overrides{});
    Offset none5[PANELS];
    Fit pf[PANELS];
    fitAll(box, none5, pf);
    CHECK(near(pf[0].azDeg, -24.2f, 0.1f) && near(pf[0].elDeg, 7.2f, 0.1f));  // tower
    CHECK(near(pf[1].azDeg, 20.7f, 0.1f) && near(pf[1].elDeg, 6.9f, 0.1f));   // sectors
    CHECK(near(pf[2].azDeg, 0, 0.1f) && near(pf[2].elDeg, -10.5f, 0.1f));     // pedals
    {
        const auto head = place(pf[2], Offset{}), seat = place(pf[2], defaultOffset(2));
        CHECK(near(head.y, seat.y, 0.005f) && near(head.z, seat.z, 0.005f) && near(head.pitchDeg, seat.pitchDeg, 0.2f));
        CHECK(!seat.headLocked && near(seat.widthM, 0.249f, 0.002f));
    }
    // stacked panels: car under the tower (left edges level), status under the sectors (right edges level)
    CHECK(near(pf[3].azDeg - pf[3].widthDeg / 2, pf[0].azDeg - pf[0].widthDeg / 2));
    CHECK(pf[3].elDeg + pf[3].heightDeg / 2 < pf[0].elDeg - pf[0].heightDeg / 2);
    CHECK(near(pf[4].azDeg + pf[4].widthDeg / 2, pf[1].azDeg + pf[1].widthDeg / 2));
    CHECK(pf[4].elDeg + pf[4].heightDeg / 2 < pf[1].elDeg - pf[1].heightDeg / 2);
    // moving the tower takes the car panel with it
    none5[0].daz = 3;
    Fit moved[PANELS];
    fitAll(box, none5, moved);
    CHECK(near(moved[3].azDeg, pf[3].azDeg + 3));

    // Texture scale follows pixels per degree.
    vr_overlay::HmdInfo none;
    CHECK(near(textureScale(none), 1));
    CHECK(textureScale(wide) > 1 && textureScale(wide) <= 2);
    vr_overlay::HmdInfo low = wide;
    low.renderW = 1500;
    CHECK(near(textureScale(low), 1));

    // Overrides from the ini.
    Overrides ov;
    ov.fovH = 140; ov.fovV = 100; ov.renderW = 3800;
    auto h = applyOverrides(none, ov);
    CHECK(near(h.left, -70) && near(h.right, 70) && near(h.up, 50) && h.renderW == 3800);
    CHECK(h.binoRight < h.right);

    std::printf(failures ? "%d FAILED\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
