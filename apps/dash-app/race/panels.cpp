#include "panels.hpp"

#include "carwire.hpp"
#include "tyrecol.hpp"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace panels {

using timing::Mark;
using timing::Snapshot;

namespace {

float g_scale = 1;
float fs(const ImFont *f) { return f->FontSize / g_scale; } // font size in layout units

// Real-dash look: black glass, white type, colour only where it means something.
const ImU32 BG = IM_COL32(8, 9, 11, 255); // panel glass; its opacity comes from the background slider
const ImU32 PLAYER_BAND = IM_COL32(242, 244, 246, 245);
const ImU32 INK = IM_COL32(10, 11, 13, 255); // text on the player's band
const ImU32 LINE = IM_COL32(255, 255, 255, 28);
const ImU32 WHITE = IM_COL32(242, 244, 246, 255);
const ImU32 DIM = IM_COL32(140, 148, 158, 255);
const ImU32 PURPLE = IM_COL32(176, 107, 255, 255);
const ImU32 GREEN = IM_COL32(49, 224, 122, 255);
const ImU32 YELLOW = IM_COL32(255, 210, 58, 255);
const ImU32 RED = IM_COL32(255, 59, 59, 255);
const ImU32 PIT = IM_COL32(58, 168, 255, 255);

// Team colours by F1 25 team id (0 Mercedes ... 9 Sauber); anything else is grey.
ImU32 teamColour(uint8_t id)
{
    static const ImU32 c[] = {
        IM_COL32(39, 244, 210, 255), IM_COL32(232, 0, 32, 255),   IM_COL32(54, 113, 198, 255),
        IM_COL32(100, 196, 255, 255), IM_COL32(34, 153, 113, 255), IM_COL32(255, 135, 188, 255),
        IM_COL32(102, 146, 255, 255), IM_COL32(182, 186, 189, 255), IM_COL32(255, 128, 0, 255),
        IM_COL32(82, 226, 82, 255),
    };
    return id < 10 ? c[id] : IM_COL32(120, 120, 120, 255);
}

ImU32 markColour(Mark m)
{
    switch (m) {
    case Mark::SessionBest: return PURPLE;
    case Mark::PersonalBest: return GREEN;
    case Mark::Slower: return YELLOW;
    case Mark::Invalid: return RED;
    default: return DIM;
    }
}

const char *tyreLetter(uint8_t visual)
{
    switch (visual) {
    case 16: return "S";
    case 17: return "M";
    case 18: return "H";
    case 7: return "I";
    case 8: return "W";
    default: return "";
    }
}

ImU32 tyreColour(uint8_t visual)
{
    switch (visual) {
    case 16: return RED;
    case 17: return YELLOW;
    case 18: return WHITE;
    case 7: return GREEN;
    case 8: return PIT;
    default: return DIM;
    }
}

void text(ImDrawList *dl, ImFont *f, float x, float y, ImU32 col, const char *s)
{
    dl->AddText(f, fs(f), ImVec2(x, y), col, s);
}

void textRight(ImDrawList *dl, ImFont *f, float right, float y, ImU32 col, const char *s)
{
    float w = f->CalcTextSizeA(fs(f), 1e9f, 0, s).x;
    dl->AddText(f, fs(f), ImVec2(right - w, y), col, s);
}

void textCentre(ImDrawList *dl, ImFont *f, float cx, float y, ImU32 col, const char *s)
{
    float w = f->CalcTextSizeA(fs(f), 1e9f, 0, s).x;
    dl->AddText(f, fs(f), ImVec2(cx - w / 2, y), col, s);
}

// Panel backgrounds go on their own draw list so the background and content opacities can be set separately.
ImDrawList *g_bg = nullptr;

void panelBg(ImDrawList *, const Rect &r)
{
    g_bg->AddRectFilled(ImVec2(r.x, r.y), ImVec2(r.x + r.w, r.y + r.h), BG, 10.f);
}

// ---- timing tower --------------------------------------------------------------------------------------------
void tower(ImDrawList *dl, const Snapshot &s, const Fonts &f, GapMode mode)
{
    const Rect r = TOWER;
    const auto all = s.order();
    const auto order = s.compactOrder(6); // leader, two ahead, you, two behind
    const int rows = std::max<int>(int(order.size()), 1);
    const float head = 44, rowH = 50;
    const float actualH = head + rowH * float(rows) + 10;
    panelBg(dl, {r.x, r.y, r.w, actualH});

    char lap[32];
    uint8_t leaderLap = all.empty() ? 0 : s.cars[all[0]].lapNum;
    std::snprintf(lap, sizeof lap, "LAP %u", leaderLap);
    text(dl, f.small, r.x + 16, r.y + 12, DIM, lap);
    if (s.session.valid && s.session.timeLeft > 0) { // the session's remaining time
        const unsigned t = unsigned(std::floor(s.session.remaining()));
        char tl[24];
        if (t >= 3600) std::snprintf(tl, sizeof tl, "%u:%02u:%02u", t / 3600, t / 60 % 60, t % 60);
        else std::snprintf(tl, sizeof tl, "%u:%02u", t / 60, t % 60);
        textCentre(dl, f.body, r.x + r.w / 2, r.y + 6, t < 300 ? YELLOW : WHITE, tl);
    }
    const char *modeLabel = mode == GapMode::Relative ? "TO YOU" : mode == GapMode::Leader ? "LEADER" : "INTERVAL";
    textRight(dl, f.small, r.x + r.w - 16, r.y + 12, DIM, modeLabel);
    dl->AddLine(ImVec2(r.x + 10, r.y + head - 4), ImVec2(r.x + r.w - 10, r.y + head - 4), LINE);

    if (order.empty()) {
        text(dl, f.body, r.x + 16, r.y + head + 6, DIM, "Waiting for telemetry");
        return;
    }
    const float ty = (rowH - fs(f.body)) / 2;
    for (int k = 0; k < int(order.size()); k++) {
        const int i = order[k];
        const auto &c = s.cars[i];
        const float y = r.y + head + rowH * float(k);
        // a break in the positions (e.g. P1 then P8): dotted separator above this row
        if (k > 0 && c.position != s.cars[order[k - 1]].position + 1)
            for (float x = r.x + 20; x < r.x + r.w - 20; x += 10)
                dl->AddCircleFilled(ImVec2(x, y), 1.6f, DIM);
        // The player's row is inverted (white band, dark type) with a marker, so it jumps out of the list.
        const bool me = i == s.playerIdx;
        const ImU32 fg = me ? INK : WHITE;
        if (me) {
            dl->AddRectFilled(ImVec2(r.x + 4, y + 1), ImVec2(r.x + r.w - 4, y + rowH - 1), PLAYER_BAND, 5.f);
            const float my = y + rowH / 2;
            dl->AddTriangleFilled(ImVec2(r.x + 8, my - 9), ImVec2(r.x + 8, my + 9), ImVec2(r.x + 19, my), INK);
        }

        char pos[8];
        std::snprintf(pos, sizeof pos, "%u", c.position);
        // practice / qualifying: a car in the pit lane keeps its time; its position goes white on blue instead
        const bool pitCell = c.pitStatus && timing::rankedOnBestLap(s);
        if (pitCell) dl->AddRectFilled(ImVec2(r.x + 22, y + 4), ImVec2(r.x + 55, y + rowH - 4), PIT, 5.f);
        textRight(dl, f.body, r.x + 50, y + ty, pitCell ? WHITE : fg, pos);
        dl->AddRectFilled(ImVec2(r.x + 60, y + 7), ImVec2(r.x + (me ? 68 : 65), y + rowH - 7), teamColour(c.teamId));
        // your own name may be a Chinese placeholder ("默认·玩家"): YOU then
        std::string code = timing::driverCode(c);
        if (me && code[0] == '#') code = "YOU";
        text(dl, f.body, r.x + 76, y + ty, fg, code.c_str());

        // best lap marker: purple dot for the session's fastest lap
        if (c.bestLapMs && c.bestLapMs == s.sessionBestLapMs)
            dl->AddCircleFilled(ImVec2(r.x + 148, y + rowH / 2), 5.f, PURPLE);

        // gap
        const char *gapText = nullptr;
        char gap[24];
        ImU32 gapCol = fg;
        const bool timed = timing::rankedOnBestLap(s);
        if (c.pitStatus && !timed) { gapText = c.pitStatus == 1 ? "PIT" : "PIT LANE"; gapCol = PIT; } // race
        else if (timed) { // practice / qualifying: best-lap differences
            const int ref = mode == GapMode::Relative ? int(s.playerIdx) : mode == GapMode::Leader ? all[0] : timing::carAhead(s, i);
            const auto g = timing::bestLapGap(s, i, ref);
            if (!c.bestLapMs) { gapText = "NO TIME"; gapCol = me ? INK : DIM; }
            else if (mode != GapMode::Relative && c.position == 1) { gapText = "LEADER"; gapCol = me ? INK : DIM; }
            else if (!g.valid) { gapText = "--"; gapCol = me ? INK : DIM; }
            else if (mode == GapMode::Relative) std::snprintf(gap, sizeof gap, "%s", timing::formatSignedGap(g.ms).c_str()), gapText = gap;
            else std::snprintf(gap, sizeof gap, "%s", timing::formatGap(uint32_t(std::max(0, g.ms))).c_str()), gapText = gap;
        }
        else if (c.resultStatus >= 4) { gapText = "OUT"; gapCol = me ? INK : DIM; }
        else if (mode == GapMode::Relative) {
            // you are +0.000; cars ahead negative, behind positive
            const auto g = timing::relativeToPlayer(s, i);
            if (!g.valid) { gapText = "--"; gapCol = me ? INK : DIM; }
            else if (g.laps != 0) std::snprintf(gap, sizeof gap, "%+d LAP", g.laps), gapText = gap;
            else std::snprintf(gap, sizeof gap, "%s", timing::formatSignedGap(g.ms).c_str()), gapText = gap;
        }
        else if (c.position == 1) { gapText = "LEADER"; gapCol = me ? INK : DIM; }
        else {
            const bool toLeader = mode == GapMode::Leader;
            uint32_t ms = toLeader ? c.gapToLeaderMs : c.gapToCarAheadMs;
            const int lapsDown = timing::lapsDown(s, i);
            if (toLeader && lapsDown > 0) std::snprintf(gap, sizeof gap, "+%d LAP", lapsDown);
            else std::snprintf(gap, sizeof gap, "%s", timing::formatGap(ms).c_str());
            gapText = gap;
        }
        textRight(dl, f.body, r.x + r.w - 98, y + ty, gapCol, gapText);

        // tyre compound, then the laps on this set
        const char *t = tyreLetter(c.visualTyre);
        if (*t) {
            const ImVec2 cc(r.x + r.w - 72, y + rowH / 2);
            const ImU32 ring = me && c.visualTyre == 18 ? INK : tyreColour(c.visualTyre); // hard is white: dark on the band
            dl->AddCircle(cc, rowH * 0.32f, ring, 0, 3.f);
            textCentre(dl, f.small, cc.x, cc.y - fs(f.small) / 2, fg, t);
            char age[8];
            std::snprintf(age, sizeof age, "%u", unsigned(c.tyreAgeLaps));
            textRight(dl, f.body, r.x + r.w - 14, y + ty, me ? INK : DIM, age);
        }
    }
}

// ---- sectors (player) ----------------------------------------------------------------------------------------
void sectors(ImDrawList *dl, const Snapshot &s, const Fonts &f)
{
    const Rect r = SECTORS;
    panelBg(dl, r);
    if (s.playerIdx >= f1tel::MAX_CARS || !s.cars[s.playerIdx].active) {
        text(dl, f.body, r.x + 20, r.y + 20, DIM, "Waiting for telemetry");
        return;
    }
    const int p = s.playerIdx;
    const auto &c = s.cars[p];

    // header: position, lap, running lap time
    char head[48];
    std::snprintf(head, sizeof head, "P%u   LAP %u", c.position, c.lapNum);
    text(dl, f.small, r.x + 20, r.y + 14, DIM, head);
    textRight(dl, f.big, r.x + r.w - 20, r.y + 8, c.currentLapInvalid ? RED : WHITE,
              timing::formatLap(c.currentLapMs).c_str());

    // three sector boxes for the running lap, then the last completed lap below
    const float bx = r.x + 20, bw = (r.w - 40 - 2 * 12) / 3, by = r.y + 76;
    const timing::LapRecord *last = c.laps.empty() ? nullptr : &c.laps.back();
    for (int k = 0; k < 3; k++) {
        const float x = bx + float(k) * (bw + 12);
        uint32_t ms = 0;
        bool valid = !c.currentLapInvalid;
        bool running = k == c.sector;
        if (k < 2 && k < c.sector) ms = c.currentSectorMs[k];
        // before a sector is done this lap, show the last lap's time dimmed
        bool fromLast = false;
        if (!ms && !running && last) { ms = last->sectorMs[k]; valid = last->sectorValid[k]; fromLast = true; }
        Mark m = timing::markSector(s, p, k, ms, valid);
        ImU32 col = markColour(m);
        dl->AddRectFilled(ImVec2(x, by), ImVec2(x + bw, by + 8), running ? WHITE : col, 3.f);
        char lbl[8];
        std::snprintf(lbl, sizeof lbl, "S%d", k + 1);
        text(dl, f.small, x, by + 18, DIM, lbl);
        textRight(dl, f.body, x + bw, by + 46, fromLast ? DIM : (ms ? col : DIM),
                  running ? "..." : timing::formatSector(ms).c_str());
        // personal best for the sector, small
        char best[24];
        std::snprintf(best, sizeof best, "PB %s", timing::formatSector(c.bestSectorMs[k]).c_str());
        textRight(dl, f.small, x + bw, by + 18, DIM, best);
    }

    // footer: last / best lap and session best
    const float fy = r.y + r.h - 96;
    dl->AddLine(ImVec2(r.x + 14, fy), ImVec2(r.x + r.w - 14, fy), LINE);
    struct Col { const char *label; uint32_t ms; ImU32 col; } cols[3] = {
        {"LAST", c.lastLapMs, last ? markColour(timing::markLap(s, p, last->lapMs, last->lapValid)) : WHITE},
        {"BEST", c.bestLapMs, c.bestLapMs && c.bestLapMs == s.sessionBestLapMs ? PURPLE : GREEN},
        {"SESSION BEST", s.sessionBestLapMs, PURPLE},
    };
    const float cw = (r.w - 40) / 3;
    for (int k = 0; k < 3; k++) {
        const float x = r.x + 20 + cw * float(k);
        text(dl, f.small, x, fy + 14, DIM, cols[k].label);
        text(dl, f.body, x, fy + 44, cols[k].ms ? cols[k].col : DIM, timing::formatLap(cols[k].ms).c_str());
    }
}

// ---- pedals (player): neon HUD bar — outlined cells, bracket frame, tick ruler --------------------------------
// Brake cells light from the centre to the left, throttle cells from the centre to the right. ImGui has no blur, so
// glow is faked by stacking wider, fainter strokes under the bright line.
ImU32 withAlpha(ImU32 c, int a) { return (c & 0x00FFFFFFu) | (ImU32(a) << 24); }

void glowText(ImDrawList *dl, ImFont *f, ImVec2 at, ImU32 col, const char *t)
{
    const float sz = fs(f);
    for (const ImVec2 d : {ImVec2(-1.5f, 0), ImVec2(1.5f, 0), ImVec2(0, -1.5f), ImVec2(0, 1.5f)})
        dl->AddText(f, sz, ImVec2(at.x + d.x, at.y + d.y), withAlpha(col, 55), t);
    dl->AddText(f, sz, at, col, t);
}

// Engine rpm colour from the game's shift lights: white, then green, red, and a flashing violet at the shift point.
ImU32 rpmColour(uint8_t revLights)
{
    if (revLights >= 96) return std::fmod(ImGui::GetTime(), 0.16) < 0.08 ? IM_COL32(170, 120, 255, 255) : IM_COL32(90, 60, 160, 255);
    if (revLights >= 80) return RED;
    if (revLights >= 55) return GREEN;
    return WHITE;
}

ImU32 lighten(ImU32 c, float t) // blend towards white
{
    auto ch = [&](int shift) { int v = int((c >> shift) & 0xFF); return ImU32(v + int((255 - v) * t)) << shift; };
    return ch(0) | ch(8) | ch(16) | (c & 0xFF000000u);
}

// One continuous bar: brake cells grow left from the centre, throttle cells right. Slanted hatched cells between two
// rails, a centred tab on the upper rail holding the rpm, chevrons and percentages at the two outer ends.
void pedals(ImDrawList *dl, const Snapshot &s, const Fonts &f)
{
    const Rect r = PEDALS;
    panelBg(dl, r);
    const float brake = std::clamp(s.player.brake, 0.f, 1.f), thr = std::clamp(s.player.throttle, 0.f, 1.f);
    const ImU32 frame = IM_COL32(200, 206, 214, 150), dim = IM_COL32(200, 206, 214, 70);

    const float cx = r.x + r.w / 2, xL = r.x + 40, xR = r.x + r.w - 40, half = cx - xL;
    const float top = r.y + 8, tabH = 34, railY = top + tabH + 6, cellY = railY + 8, cellH = 34, botY = cellY + cellH + 8;

    // centred tab on the upper rail, holding the rpm (coloured by the shift lights)
    char rt[8];
    std::snprintf(rt, sizeof rt, "%u", unsigned(s.player.rpm));
    const float rw = f.body->CalcTextSizeA(fs(f.body), 1e9f, 0, rt).x;
    const float tabTop = 70, slope = 20; // half-width of the tab's top edge, and the run of its slanted sides
    const ImVec2 tab[4] = {{cx - tabTop - slope, railY}, {cx - tabTop, top}, {cx + tabTop, top}, {cx + tabTop + slope, railY}};
    dl->AddConvexPolyFilled(tab, 4, IM_COL32(255, 255, 255, 14));
    dl->AddPolyline(tab, 4, frame, 0, 1.5f);
    glowText(dl, f.body, ImVec2(cx - rw / 2, top + (tabH - fs(f.body)) / 2 + 1), rpmColour(s.player.revLights), rt);

    // upper rail either side of the tab, each with a thick slanted accent in its pedal's colour
    dl->AddLine(ImVec2(xL, railY), ImVec2(cx - tabTop - slope, railY), dim, 1.f);
    dl->AddLine(ImVec2(cx + tabTop + slope, railY), ImVec2(xR, railY), dim, 1.f);
    for (int dir : {-1, 1}) {
        const ImU32 col = dir < 0 ? RED : GREEN;
        const bool on = (dir < 0 ? brake : thr) > 0.001f;
        auto X = [&](float d) { return cx + float(dir) * d; };
        const float d0 = tabTop + slope + 14, d1 = half * 0.78f;
        const ImVec2 acc[4] = {{X(d0), railY - 1}, {X(d0 + 8), railY - 6}, {X(d1), railY - 6}, {X(d1 + 8), railY - 1}};
        dl->AddConvexPolyFilled(acc, 4, withAlpha(col, on ? 200 : 90));
    }

    // cells: 16 per side, slanted parallelograms leaning outwards; lit ones hatched, the last lit one partial
    const int cells = 16;
    const float gap = 7, skew = cellH * 0.45f, inner = 6;
    const float cw = (half - inner - skew - gap * float(cells - 1)) / float(cells);
    auto side = [&](int dir, float v, ImU32 col) {
        auto X = [&](float d) { return cx + float(dir) * d; };
        auto quad = [&](float a0, float a1, ImVec2 *q) {
            q[0] = ImVec2(X(a0), cellY + cellH); q[1] = ImVec2(X(a0 + skew), cellY);
            q[2] = ImVec2(X(a1 + skew), cellY);  q[3] = ImVec2(X(a1), cellY + cellH);
        };
        const float lit = v * float(cells);
        for (int k = 0; k < cells; k++) {
            const float d0 = inner + float(k) * (cw + gap);
            ImVec2 q[4];
            quad(d0, d0 + cw, q);
            const float fill = std::clamp(lit - float(k), 0.f, 1.f);
            if (fill <= 0) {
                dl->AddPolyline(q, 4, withAlpha(col, 60), ImDrawFlags_Closed, 1.2f);
                continue;
            }
            ImVec2 fq[4];
            quad(d0, d0 + cw * fill, fq);
            dl->AddConvexPolyFilled(fq, 4, withAlpha(col, 210));
            for (float t : {0.3f, 0.65f}) { // hatching, parallel to the slant
                if (t > fill) break;
                const float h = d0 + cw * t;
                dl->AddLine(ImVec2(X(h), cellY + cellH - 2), ImVec2(X(h + skew), cellY + 2), withAlpha(lighten(col, 0.6f), 200), 2.f);
            }
            dl->AddPolyline(q, 4, lighten(col, 0.35f), ImDrawFlags_Closed, 1.5f);
        }
    };
    side(-1, brake, RED);
    side(+1, thr, GREEN);
    dl->AddLine(ImVec2(cx, cellY - 2), ImVec2(cx, cellY + cellH + 2), withAlpha(frame, 160), 1.5f); // centre

    // lower rail: one thin box with an accent per side, ticks under it
    dl->AddRect(ImVec2(xL, botY), ImVec2(xR, botY + 6), dim, 0, 0, 1.f);
    for (int dir : {-1, 1}) {
        const ImU32 col = dir < 0 ? RED : GREEN;
        const bool on = (dir < 0 ? brake : thr) > 0.001f;
        auto X = [&](float d) { return cx + float(dir) * d; };
        const ImVec2 acc[4] = {{X(half * 0.25f), botY}, {X(half * 0.25f + 10), botY + 8},
                               {X(half * 0.6f - 10), botY + 8}, {X(half * 0.6f), botY}};
        dl->AddConvexPolyFilled(acc, 4, withAlpha(col, on ? 200 : 90));
    }
    for (int t = 0; t <= 80; t++) {
        const float x = xL + (xR - xL) * float(t) / 80.f;
        dl->AddLine(ImVec2(x, botY + 11), ImVec2(x, botY + (t % 5 == 0 ? 19.f : 15.f)), withAlpha(frame, t % 5 == 0 ? 150 : 80), 1.f);
    }

    // chevrons at the outer ends of the cells; percentages above the bar at its two ends
    const float midY = cellY + cellH / 2;
    for (int dir : {-1, 1}) {
        const float x = dir < 0 ? xL - 8 : xR + 8;
        dl->AddLine(ImVec2(x - float(dir) * 8, midY - 14), ImVec2(x, midY), frame, 1.5f);
        dl->AddLine(ImVec2(x, midY), ImVec2(x - float(dir) * 8, midY + 14), frame, 1.5f);
    }
    char bt[8], tt[8];
    std::snprintf(bt, sizeof bt, "%d%%", int(std::lround(brake * 100)));
    std::snprintf(tt, sizeof tt, "%d%%", int(std::lround(thr * 100)));
    const float ty = top + (tabH - fs(f.body)) / 2 + 1;
    const float tw = f.body->CalcTextSizeA(fs(f.body), 1e9f, 0, tt).x;
    glowText(dl, f.body, ImVec2(xL + 4, ty), brake > 0.001f ? RED : withAlpha(RED, 80), bt);
    glowText(dl, f.body, ImVec2(xR - 4 - tw, ty), thr > 0.001f ? GREEN : withAlpha(GREEN, 80), tt);
}

// ---- car: tyres and damage -------------------------------------------------------------------------------------
// A top view of the car: each tyre coloured by inner (carcass) temperature;
// wings, floor, diffuser and sidepods coloured by damage. Beside each tyre its numbers;
// the damaged parts and failures are listed on the status panel.
ImU32 damageCol(int pct)
{
    if (pct < 1) return withAlpha(GREEN, 150);
    if (pct < 20) return YELLOW;
    if (pct < 50) return IM_COL32(255, 140, 40, 255);
    return RED;
}

void car(ImDrawList *dl, const Snapshot &s, const Fonts &f)
{
    const Rect r = CAR;
    panelBg(dl, r);
    if (s.playerIdx >= f1tel::MAX_CARS || !s.cars[s.playerIdx].active) {
        text(dl, f.small, r.x + 16, r.y + 16, DIM, "Waiting for telemetry");
        return;
    }
    const auto &p = s.player;
    const auto &dm = s.damage;
    const uint8_t compound = s.cars[s.playerIdx].visualTyre;
    auto tempCol = [compound](float t) { return tyrecol::temp(t, compound); };

    // the car (Track N Race's wireframe), centred, in the upper part: damage zones and tyres filled
    const carwire::Placement cp = carwire::fit(ImVec2(r.x + r.w / 2 - 56, r.y + 18), ImVec2(112, 250));
    carwire::Colours cc;
    cc.bg = IM_COL32(16, 18, 24, 255);
    if (dm.valid) {
        auto dmg = [](int pct) { return withAlpha(damageCol(pct), 150); };
        cc.fill[carwire::FW_L] = dmg(dm.frontWingL);
        cc.fill[carwire::FW_R] = dmg(dm.frontWingR);
        cc.fill[carwire::FLOOR] = dmg(dm.floor);
        cc.fill[carwire::DIFF] = dmg(dm.diffuser);
        cc.fill[carwire::SP_L] = cc.fill[carwire::SP_R] = dmg(dm.sidepod);
        cc.fill[carwire::RW] = dmg(dm.rearWing);
    }
    // tyres FL FR RL RR (the game's order is RL RR FL FR) with their numbers beside them
    const int idx[4] = {2, 3, 0, 1};
    const carwire::Part tyrePart[4] = {carwire::T_FL, carwire::T_FR, carwire::T_RL, carwire::T_RR};
    for (int k = 0; k < 4; k++) cc.fill[tyrePart[k]] = withAlpha(tempCol(p.tyreInner[idx[k]]), 220);
    carwire::draw(dl, cp, cc);
    for (int k = 0; k < 4; k++) {
        const int i = idx[k];
        const bool right = k % 2 == 1;
        ImVec2 ta, tb;
        carwire::box(tyrePart[k], cp, ta, tb);
        const float x0 = ta.x, t0 = ta.y, t1 = tb.y, tw = tb.x - ta.x;
        if (dm.valid && (dm.tyreDamage[i] >= 20 || dm.blisters[i] >= 20)) // a damaged tyre gets a red outline
            dl->AddRect(ImVec2(x0 - 3, t0 - 3), ImVec2(x0 + tw + 3, t1 + 3), RED, 7, 0, 2.5f);

        // wear and inner temperature large, the brake temperature small; all coloured
        const float nx = right ? x0 + tw + 16 : x0 - 16;
        auto put = [&](ImFont *fo, float size, float y, ImU32 col, const char *t) {
            const float w = right ? 0 : fo->CalcTextSizeA(size, 1e9f, 0, t).x;
            dl->AddText(fo, size, ImVec2(nx - w, y), col, t);
        };
        char b[32];
        float y = t0 - 8;
        std::snprintf(b, sizeof b, "%.0f%%", p.tyreWear[i]);
        put(f.body, fs(f.body), y, tyrecol::wear(p.tyreWear[i]), b);
        y += fs(f.body);
        std::snprintf(b, sizeof b, "%u\xC2\xB0", unsigned(p.tyreInner[i]));
        put(f.body, fs(f.body), y, tempCol(p.tyreInner[i]), b);
        y += fs(f.body);
        std::snprintf(b, sizeof b, "%u\xC2\xB0", unsigned(p.brakeTemp[i]));
        put(f.small, fs(f.small) * 0.8f, y + 2, tyrecol::brake(p.brakeTemp[i]), b);
    }
}

// ---- status: power unit wear, damaged parts, failures ----------------------------------------------------------
// Top: wear of each power unit part, gearbox and engine as small bars. Below: what is damaged or has failed, worst
// first (failures in red ahead of everything).
void status(ImDrawList *dl, const Snapshot &s, const Fonts &f)
{
    const Rect r = STATUS;
    panelBg(dl, r);
    const auto &dm = s.damage;
    if (!dm.valid) {
        text(dl, f.small, r.x + 20, r.y + 16, DIM, "No damage data");
        return;
    }

    // wear bars: 4 per row
    struct Wear {
        const char *name;
        int v;
        bool pu; // power unit part: wear (cyan, orange from 50 %, red from 75 %); else damage colours
    } wear[] = {{"ICE", dm.ice, true}, {"MGU-H", dm.mguh, true}, {"MGU-K", dm.mguk, true}, {"ES", dm.es, true},
                {"CE", dm.ce, true},   {"TC", dm.tc, true},     {"GBX", dm.gearbox, false}, {"ENG", dm.engine, false}};
    const float x0 = r.x + 20, cw = (r.w - 40) / 4;
    for (int k = 0; k < 8; k++) {
        const float x = x0 + cw * float(k % 4), y = r.y + 12 + float(k / 4) * 44;
        const auto &w = wear[k];
        const ImU32 col = w.pu ? (w.v < 50 ? IM_COL32(80, 210, 230, 255) : w.v < 75 ? IM_COL32(255, 140, 40, 255) : RED)
                               : damageCol(w.v);
        text(dl, f.small, x, y, DIM, w.name);
        char b[16];
        std::snprintf(b, sizeof b, "%d%%", w.v);
        textRight(dl, f.small, x + cw - 14, y, WHITE, b);
        const float by = y + fs(f.small) + 4, bw = cw - 14;
        dl->AddRectFilled(ImVec2(x, by), ImVec2(x + bw, by + 6), IM_COL32(255, 255, 255, 24), 2);
        dl->AddRectFilled(ImVec2(x, by), ImVec2(x + bw * std::clamp(float(w.v) / 100.f, 0.f, 1.f), by + 6), col, 2);
    }

    // damaged parts and failures
    const float ly = r.y + 104;
    dl->AddLine(ImVec2(r.x + 14, ly), ImVec2(r.x + r.w - 14, ly), LINE);
    struct Item {
        const char *name;
        int pct;
        bool fail;
    };
    std::vector<Item> items;
    if (dm.engineBlown) items.push_back({"ENGINE BLOWN", 100, true});
    if (dm.engineSeized) items.push_back({"ENGINE SEIZED", 100, true});
    const Item parts[] = {{"FW L", dm.frontWingL, false}, {"FW R", dm.frontWingR, false}, {"RW", dm.rearWing, false},
                          {"FLOOR", dm.floor, false},     {"DIFF", dm.diffuser, false},   {"POD", dm.sidepod, false}};
    for (const auto &it : parts)
        if (it.pct > 0) items.push_back(it);
    static const char *tn[4] = {"TYRE FL", "TYRE FR", "TYRE RL", "TYRE RR"};
    const int idx[4] = {2, 3, 0, 1};
    for (int k = 0; k < 4; k++) {
        const int d = std::max(std::max(dm.tyreDamage[idx[k]], dm.blisters[idx[k]]), dm.brakeDamage[idx[k]]);
        if (d > 0) items.push_back({tn[k], d, false});
    }
    std::stable_sort(items.begin(), items.end(),
                     [](const Item &a, const Item &b) { return a.fail != b.fail ? a.fail : a.pct > b.pct; });
    float x = r.x + 20, y = ly + 12;
    if (items.empty()) text(dl, f.small, x, y, withAlpha(GREEN, 200), "NO DAMAGE");
    for (const auto &it : items) {
        char b[32];
        if (it.fail) std::snprintf(b, sizeof b, "%s", it.name);
        else std::snprintf(b, sizeof b, "%s %d%%", it.name, it.pct);
        const float w = f.small->CalcTextSizeA(fs(f.small), 1e9f, 0, b).x;
        if (x + w > r.x + r.w - 20) { // next line; stop when the panel is full
            x = r.x + 20;
            y += fs(f.small) + 8;
            if (y + fs(f.small) > r.y + r.h - 8) break;
        }
        text(dl, f.small, x, y, it.fail ? RED : damageCol(it.pct), b);
        x += w + 22;
    }
}

// ---- weather forecast -------------------------------------------------------------------------------------------
// This session's forecast as a row of columns: when, an icon, the chance of rain and the track temperature.
void weatherIcon(ImDrawList *dl, ImVec2 c, float sz, uint8_t w)
{
    const ImU32 SUN = IM_COL32(255, 200, 60, 255), CLOUD = IM_COL32(190, 198, 208, 255),
                DARK = IM_COL32(120, 128, 140, 255), RAIN = IM_COL32(70, 160, 255, 255);
    if (w == 0) { // clear
        dl->AddCircleFilled(c, sz * 0.32f, SUN);
        for (int k = 0; k < 8; k++) {
            const float a = float(k) * 3.14159265f / 4;
            dl->AddLine(ImVec2(c.x + std::cos(a) * sz * 0.42f, c.y + std::sin(a) * sz * 0.42f),
                        ImVec2(c.x + std::cos(a) * sz * 0.55f, c.y + std::sin(a) * sz * 0.55f), SUN, 2.f);
        }
        return;
    }
    if (w == 1) dl->AddCircleFilled(ImVec2(c.x + sz * 0.18f, c.y - sz * 0.18f), sz * 0.26f, SUN); // light cloud
    const ImU32 cc = w >= 4 ? DARK : CLOUD;
    const float cy = w >= 3 ? c.y - sz * 0.12f : c.y;
    dl->AddCircleFilled(ImVec2(c.x - sz * 0.18f, cy + sz * 0.04f), sz * 0.2f, cc);
    dl->AddCircleFilled(ImVec2(c.x + sz * 0.08f, cy - sz * 0.06f), sz * 0.26f, cc);
    dl->AddRectFilled(ImVec2(c.x - sz * 0.38f, cy + sz * 0.02f), ImVec2(c.x + sz * 0.36f, cy + sz * 0.24f), cc, sz * 0.1f);
    if (w >= 3) { // rain: more drops when heavy
        const int drops = w == 3 ? 2 : 4;
        for (int k = 0; k < drops; k++) {
            const float x = c.x - sz * 0.27f + float(k) * sz * 0.54f / float(std::max(1, drops - 1));
            dl->AddLine(ImVec2(x, cy + sz * 0.32f), ImVec2(x - sz * 0.08f, cy + sz * 0.5f), RAIN, 2.f);
        }
    }
    if (w == 5) { // storm: a bolt
        const ImVec2 a(c.x + sz * 0.02f, cy + sz * 0.22f), b(c.x - sz * 0.1f, cy + sz * 0.4f), d(c.x + sz * 0.04f, cy + sz * 0.4f),
            e(c.x - sz * 0.08f, cy + sz * 0.6f);
        dl->AddLine(a, b, SUN, 2.5f);
        dl->AddLine(b, d, SUN, 2.5f);
        dl->AddLine(d, e, SUN, 2.5f);
    }
}

void weather(ImDrawList *dl, const Snapshot &s, const Fonts &f)
{
    const Rect r = WEATHER;
    panelBg(dl, r);
    const auto &fc = s.session.forecast;
    if (!s.session.valid || fc.empty()) {
        textCentre(dl, f.small, r.x + r.w / 2, r.y + r.h / 2 - fs(f.small) / 2, DIM, "No forecast");
        return;
    }
    // every sample of this session (up to 9: now .. +120'), so a change late in the race (clearing up) is on it too
    const int n = std::min<int>(int(fc.size()), 9);
    const float cw = (r.w - 24) / float(n);
    const bool narrow = cw < 80; // no room for the temperature: rain chance only
    for (int k = 0; k < n; k++) {
        const auto &w = fc[k];
        const float cx = r.x + 12 + cw * (float(k) + 0.5f);
        if (k) dl->AddLine(ImVec2(cx - cw / 2, r.y + 12), ImVec2(cx - cw / 2, r.y + r.h - 12), LINE);
        char b[24];
        if (w.minutes == 0) std::snprintf(b, sizeof b, "NOW");
        else std::snprintf(b, sizeof b, "+%u'", unsigned(w.minutes));
        textCentre(dl, f.small, cx, r.y + 6, w.minutes == 0 ? WHITE : DIM, b);
        weatherIcon(dl, ImVec2(cx, r.y + 50), std::min(36.f, cw * 0.62f), w.weather);
        std::snprintf(b, sizeof b, "%u%%", unsigned(w.rain));
        textCentre(dl, f.small, narrow ? cx : cx - cw * 0.2f, r.y + r.h - 28, w.rain >= 50 ? PIT : w.rain > 0 ? WHITE : DIM, b);
        if (!narrow) {
            std::snprintf(b, sizeof b, "%d\xC2\xB0", int(w.trackTemp));
            textCentre(dl, f.small, cx + cw * 0.22f, r.y + r.h - 28, DIM, b);
        }
    }
    if (s.session.forecastAccuracy == 1) textRight(dl, f.small, r.x + r.w - 10, r.y + 6, DIM, "~");
}

} // namespace

void weatherSymbol(ImDrawList *dl, const ImVec2 &c, float sz, uint8_t w) { weatherIcon(dl, c, sz, w); }

void draw(const Snapshot &s, const Fonts &f, GapMode gaps)
{
    g_scale = f.scale;
    g_bg = ImGui::GetBackgroundDrawList();
    ImDrawList *dl = ImGui::GetForegroundDrawList();
    tower(dl, s, f, gaps);
    sectors(dl, s, f);
    pedals(dl, s, f);
    car(dl, s, f);
    status(dl, s, f);
    weather(dl, s, f);
}

void applyOpacity(ImDrawData *dd, float background, float content)
{
    const ImDrawList *bg = ImGui::GetBackgroundDrawList();
    for (ImDrawList *l : dd->CmdLists) {
        const float a = l == bg ? background : content;
        if (a >= 0.999f) continue;
        for (ImDrawVert &v : l->VtxBuffer) {
            const ImU32 alpha = ImU32(float((v.col >> IM_COL32_A_SHIFT) & 0xFF) * std::clamp(a, 0.f, 1.f) + 0.5f);
            v.col = (v.col & ~IM_COL32_A_MASK) | (alpha << IM_COL32_A_SHIFT);
        }
    }
}

// ---- demo data -----------------------------------------------------------------------------------------------
Snapshot demo(double t)
{
    static const char *names[] = {"VERSTAPPEN", "NORRIS",  "LECLERC", "PIASTRI",   "RUSSELL", "HAMILTON", "ANTONELLI",
                                  "ALONSO",     "SAINZ",   "GASLY",   "HULKENBERG", "TSUNODA", "ALBON",    "OCON",
                                  "LAWSON",     "BEARMAN", "STROLL",  "HADJAR",    "COLAPINTO", "BORTOLETO"};
    static const uint8_t teams[] = {2, 8, 1, 8, 0, 1, 0, 4, 3, 5, 9, 2, 3, 7, 6, 7, 4, 6, 5, 9};
    Snapshot s;
    s.hasData = true;
    s.numActive = 20;
    s.playerIdx = 4;
    const double lapLen = 92.0;
    const int lapNum = 12 + int(t / lapLen);
    const double inLap = std::fmod(t, lapLen);
    for (int i = 0; i < 20; i++) {
        auto &c = s.cars[i];
        c.active = true;
        c.name = names[i];
        c.teamId = teams[i];
        c.position = uint8_t(i + 1);
        c.lapNum = uint8_t(lapNum - (i >= 17 ? 1 : 0));
        c.gapToCarAheadMs = i ? uint32_t(600 + 900 * std::fabs(std::sin(i * 1.7 + t / 30))) : 0;
        c.gapToLeaderMs = 0;
        for (int k = 1; k <= i; k++) c.gapToLeaderMs += uint32_t(600 + 900 * std::fabs(std::sin(k * 1.7 + t / 30)));
        c.visualTyre = i % 3 == 0 ? 18 : 17;
        c.tyreAgeLaps = uint8_t(i % 3 == 0 ? 4 + i % 5 : 11 + i % 4);
        c.pitStatus = (i == 13 && std::fmod(t, 40) < 8) ? 1 : 0;
        c.resultStatus = 2;
        for (int l = 0; l < lapNum - 1; l++) {
            timing::LapRecord r;
            r.sectorMs = {uint32_t(29800 + i * 40 + (l * 37 + i * 11) % 400), uint32_t(37600 + i * 30 + (l * 53 + i) % 500),
                          uint32_t(24100 + i * 20 + (l * 29 + i * 7) % 300)};
            r.lapMs = r.sectorMs[0] + r.sectorMs[1] + r.sectorMs[2];
            r.lapValid = !(i == 4 && l == 6);
            r.sectorValid = {true, r.lapValid, true};
            c.laps.push_back(r);
        }
        c.bestLapMs = 0;
        c.bestSectorMs = {};
        for (const auto &r : c.laps) {
            if (r.lapValid && (!c.bestLapMs || r.lapMs < c.bestLapMs)) c.bestLapMs = r.lapMs;
            for (int k = 0; k < 3; k++)
                if (r.sectorValid[k] && (!c.bestSectorMs[k] || r.sectorMs[k] < c.bestSectorMs[k]))
                    c.bestSectorMs[k] = r.sectorMs[k];
        }
        c.lastLapMs = c.laps.empty() ? 0 : c.laps.back().lapMs;
    }
    for (const auto &c : s.cars)
        for (const auto &r : c.laps) {
            if (r.lapValid && (!s.sessionBestLapMs || r.lapMs < s.sessionBestLapMs)) s.sessionBestLapMs = r.lapMs;
            for (int k = 0; k < 3; k++)
                if (r.sectorValid[k] && (!s.sessionBestSectorMs[k] || r.sectorMs[k] < s.sessionBestSectorMs[k]))
                    s.sessionBestSectorMs[k] = r.sectorMs[k];
        }

    auto &me = s.cars[s.playerIdx];
    me.currentLapMs = uint32_t(inLap * 1000);
    me.sector = uint8_t(inLap < 30 ? 0 : inLap < 68 ? 1 : 2);
    if (me.sector >= 1) me.currentSectorMs[0] = 29850;
    if (me.sector >= 2) me.currentSectorMs[1] = 37580; // beats the session best: purple

    // a braking zone every ~9 s
    const double ph = std::fmod(t, 9.0);
    s.player.brake = ph < 1.2 ? float(std::min(1.0, ph * 4) * (1 - ph / 1.6)) : 0.f;
    s.player.throttle = ph < 1.0 ? 0.f : float(std::min(1.0, (ph - 1.0) * 1.5));
    s.player.speed = uint16_t(180 + 120 * s.player.throttle);
    s.player.gear = int8_t(4 + 4 * s.player.throttle);
    s.player.rpm = uint16_t(9800 + 2100 * s.player.throttle - 1800 * s.player.brake);
    s.player.revLights = uint8_t(std::clamp(int((s.player.rpm - 9000) / 30), 0, 100));
    for (int i = 0; i < 4; i++) {
        s.player.tyreSurface[i] = uint8_t(96 + 6 * std::sin(t / 7 + i) + (i >= 2 ? 4 : 0));
        s.player.tyreInner[i] = uint8_t(101 + 3 * std::sin(t / 11 + i));
        s.player.tyreWear[i] = float(18 + i * 3 + t / 60);
        s.player.tyrePressure[i] = 22.8f + 0.1f * float(i);
        s.player.brakeTemp[i] = uint16_t(500 + 350 * s.player.brake + 20 * i);
    }
    s.damage.valid = true;
    s.damage.frontWingL = 12;
    s.session.valid = true;
    s.session.timeLeft = uint16_t(std::max(0.0, 2400 - t));
    s.session.forecast = {{0, 1, 5, 32, 24}, {5, 2, 15, 31, 23}, {10, 2, 30, 30, 23},
                          {15, 3, 55, 28, 22}, {30, 4, 80, 26, 21}, {45, 3, 60, 27, 21}};
    s.damage.floor = 4;
    return s;
}

} // namespace panels
