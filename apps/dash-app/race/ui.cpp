#include "ui.hpp"

#include "log.hpp"

#include "compare.hpp"
#include "panels.hpp"
#include "launcher.hpp"
#include "wheelinput.hpp"
#include "tracks.hpp"
#include "carwire.hpp"
#include "tyrecol.hpp"

#include <cstring>
#include <set>
#include <imgui.h>
#include <imgui_internal.h>

#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>

#include <algorithm>
#include <cfloat>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <sstream>
#include <fstream>
#include <functional>

#ifndef F1RACE_VERSION
#define F1RACE_VERSION "dev"
#endif

namespace fs = std::filesystem;

namespace ui {

namespace {

using namespace timing;
using app::Source;

// ---- palette ---------------------------------------------------------------------------------------------------
constexpr ImU32 C_BG = IM_COL32(13, 15, 19, 255);
constexpr ImU32 C_SIDE = IM_COL32(17, 20, 25, 255);
constexpr ImU32 C_PANEL = IM_COL32(22, 26, 32, 255);
constexpr ImU32 C_PANEL2 = IM_COL32(30, 35, 43, 255);
constexpr ImU32 C_BORDER = IM_COL32(42, 48, 58, 255);
constexpr ImU32 C_GRID = IM_COL32(48, 55, 66, 255);
constexpr ImU32 C_TEXT = IM_COL32(232, 234, 237, 255);
constexpr ImU32 C_DIM = IM_COL32(135, 144, 157, 255);
constexpr ImU32 C_ACCENT = IM_COL32(232, 32, 32, 255);
constexpr ImU32 C_GREEN = IM_COL32(46, 214, 120, 255);
constexpr ImU32 C_PURPLE = IM_COL32(178, 102, 255, 255);
constexpr ImU32 C_YELLOW = IM_COL32(245, 197, 24, 255);
constexpr ImU32 C_ORANGE = IM_COL32(255, 140, 40, 255);
constexpr ImU32 C_RED = IM_COL32(255, 72, 72, 255);
constexpr ImU32 C_BLUE = IM_COL32(64, 160, 255, 255);
const timing::Snapshot *g_weatherSnap = nullptr; // what weatherStrip() draws from
void weatherStrip(ImDrawList *dl, ImVec2 p0, ImVec2 sz);
constexpr ImU32 C_CYAN = IM_COL32(60, 220, 230, 255);

ImVec4 v4(ImU32 c) { return ImGui::ColorConvertU32ToFloat4(c); }
ImU32 alpha(ImU32 c, float a)
{
    return (c & 0x00FFFFFF) | (ImU32(std::clamp(a, 0.f, 1.f) * float(c >> 24)) << 24);
}

ImU32 teamColour(uint8_t id)
{
    static const ImU32 c[] = {
        IM_COL32(39, 244, 210, 255),  IM_COL32(232, 0, 32, 255),   IM_COL32(54, 113, 198, 255),
        IM_COL32(100, 196, 255, 255), IM_COL32(34, 153, 113, 255), IM_COL32(255, 135, 188, 255),
        IM_COL32(102, 146, 255, 255), IM_COL32(182, 186, 189, 255), IM_COL32(255, 128, 0, 255),
        IM_COL32(82, 226, 82, 255),
    };
    return id < 10 ? c[id] : IM_COL32(120, 120, 120, 255);
}

ImU32 markColour(Mark m)
{
    switch (m) {
    case Mark::SessionBest: return C_PURPLE;
    case Mark::PersonalBest: return C_GREEN;
    case Mark::Slower: return C_YELLOW;
    case Mark::Invalid: return C_RED;
    default: return C_DIM;
    }
}

void tyreInfo(uint8_t visual, const char *&letter, ImU32 &col)
{
    switch (visual) {
    case 16: letter = "S"; col = IM_COL32(240, 50, 50, 255); return;
    case 17: letter = "M"; col = IM_COL32(250, 210, 40, 255); return;
    case 18: letter = "H"; col = IM_COL32(235, 235, 235, 255); return;
    case 7: letter = "I"; col = IM_COL32(60, 200, 80, 255); return;
    case 8: letter = "W"; col = IM_COL32(50, 130, 255, 255); return;
    default: letter = "?"; col = C_DIM;
    }
}

// ---- state -----------------------------------------------------------------------------------------------------
enum Page { DASH, SETUP, CAPTURE, REPLAY, COMPARE, FORWARD, HMD, SETTINGS, GAME, PAGE_COUNT }; // GAME last: page numbers of the others stay
const char *PAGE_NAMES[PAGE_COUNT] = {"Live Dash", "Car Setup", "Capture", "Replay", "Lap Compare", "Forwarding", "VR Panels", "Settings", "Game"};
const char *PAGE_HINTS[PAGE_COUNT] = {"Cars, standings and timing in the session", "Your car's setup, current settings and assists", "Choose the data source, check what arrives", "Recording library and replay controls",
                                      "Pick any two laps from recordings: lines, sectors and traces", "Send the telemetry on to other tools",
                                      "Position and opacity of the panels in the headset", "Program options",
                                      "Start F1 25: online through Steam, or offline with mods through Swofty"};
int g_page = DASH;
bool g_dragSeek = false;
uint64_t g_dragT = 0;     // where the dragged seek bar points, ns
int g_detailLaps = 5;     // laps shown on the replay page's detail bar
float g_mapZoom = 1;      // track map: 1 = whole circuit, above that centred on the player, below it smaller (room for labels)
bool g_mapFull = false;  // the dashboard as a full-screen map with the other cards floating over it
constexpr float MAP_ZOOM_MIN = 1.f / 1.5f; // one step out from the whole circuit
float g_mapPanX = 0, g_mapPanZ = 0; // track map: dragged away from the centre, world metres
float g_mapScale = 1;     // track map: pixels per metre of the last frame (for dragging)
std::string g_deletePath;

const Fonts *F = nullptr;

// A font's size in layout units (fonts are rasterised bigger and scaled down, see main.cpp).
float fsz(const ImFont *f) { return f->FontSize * f->Scale; }

// ---- small widgets -----------------------------------------------------------------------------------------------
void textCol(ImU32 c, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    ImGui::PushStyleColor(ImGuiCol_Text, c);
    ImGui::TextV(fmt, ap);
    ImGui::PopStyleColor();
    va_end(ap);
}

void withFont(ImFont *f, const std::function<void()> &fn)
{
    ImGui::PushFont(f);
    fn();
    ImGui::PopFont();
}

bool beginCard(const char *id, ImVec2 size, const char *title = nullptr)
{
    ImGui::PushStyleColor(ImGuiCol_ChildBg, v4(C_PANEL));
    ImGui::PushStyleColor(ImGuiCol_Border, v4(C_BORDER));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 6.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14, 10));
    bool open = ImGui::BeginChild(id, size, ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding,
                                  ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);
    if (title) {
        textCol(C_DIM, "%s", title);
        ImGui::Spacing();
    }
    return open;
}
void endCard() { ImGui::EndChild(); }

bool beginScrollCard(const char *id, ImVec2 size, const char *title)
{
    ImGui::PushStyleColor(ImGuiCol_ChildBg, v4(C_PANEL));
    ImGui::PushStyleColor(ImGuiCol_Border, v4(C_BORDER));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 6.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14, 10));
    bool open = ImGui::BeginChild(id, size, ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding);
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);
    if (title) {
        textCol(C_DIM, "%s", title);
        ImGui::Spacing();
    }
    return open;
}

void statCard(const char *id, float w, const char *label, const std::string &value, ImU32 col = C_TEXT,
              const char *sub = nullptr)
{
    beginCard(id, ImVec2(w, 84));
    textCol(C_DIM, "%s", label);
    withFont(F->numMid, [&] { textCol(col, "%s", value.c_str()); });
    if (sub) {
        ImGui::SameLine();
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10);
        textCol(C_DIM, "%s", sub);
    }
    endCard();
}

bool accentButton(const char *label, ImVec2 size = ImVec2(0, 0), ImU32 col = C_ACCENT)
{
    ImGui::PushStyleColor(ImGuiCol_Button, v4(alpha(col, 0.85f)));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, v4(col));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, v4(alpha(col, 0.7f)));
    ImGui::PushStyleColor(ImGuiCol_Text, v4(IM_COL32_WHITE));
    bool r = ImGui::Button(label, size);
    ImGui::PopStyleColor(4);
    return r;
}

void hbar(float frac, ImU32 col, float h, const char *label = nullptr)
{
    ImDrawList *dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    float w = ImGui::GetContentRegionAvail().x;
    dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), C_PANEL2, 3);
    dl->AddRectFilled(p, ImVec2(p.x + w * std::clamp(frac, 0.f, 1.f), p.y + h), col, 3);
    if (label) {
        ImVec2 ts = ImGui::CalcTextSize(label);
        dl->AddText(ImVec2(p.x + 8, p.y + (h - ts.y) / 2), C_TEXT, label);
    }
    ImGui::Dummy(ImVec2(w, h));
}

void openFolder(const std::string &dir)
{
    fs::create_directories(dir);
    ShellExecuteW(nullptr, L"open", fs::path(dir).wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

std::string pickRecording(const std::string &dir)
{
    wchar_t file[MAX_PATH] = L"";
    std::wstring wdir = fs::path(dir).wstring();
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof ofn;
    ofn.lpstrFilter = L"F1 telemetry recording (*.f1tr)\0*.f1tr\0All files\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrInitialDir = wdir.c_str();
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&ofn)) return {};
    return fs::path(file).string();
}

// ---- plots -----------------------------------------------------------------------------------------------------
struct Plot {
    ImVec2 p0, p1;
    float x0, x1, y0, y1;
    ImDrawList *dl;
    bool hovered;
    bool active; // left button held on it
    ImVec2 map(float x, float y) const
    {
        return ImVec2(p0.x + (x - x0) / (x1 - x0) * (p1.x - p0.x), p1.y - (y - y0) / (y1 - y0) * (p1.y - p0.y));
    }
    float unmapX(float sx) const { return x0 + (sx - p0.x) / (p1.x - p0.x) * (x1 - x0); }
};

Plot beginPlot(const char *id, float h, float x0, float x1, float y0, float y1, const char *label, int hlines = 4)
{
    Plot p;
    p.dl = ImGui::GetWindowDrawList();
    ImVec2 c = ImGui::GetCursorScreenPos();
    float w = ImGui::GetContentRegionAvail().x;
    ImGui::InvisibleButton(id, ImVec2(w, h));
    p.hovered = ImGui::IsItemHovered();
    p.active = ImGui::IsItemActive();
    const float left = 54;
    p.p0 = ImVec2(c.x + left, c.y + 4);
    p.p1 = ImVec2(c.x + w - 6, c.y + h - 4);
    p.x0 = x0; p.x1 = x1 > x0 ? x1 : x0 + 1;
    p.y0 = y0; p.y1 = y1 > y0 ? y1 : y0 + 1;
    p.dl->AddRectFilled(ImVec2(c.x, c.y), ImVec2(c.x + w, c.y + h), C_PANEL, 4);
    p.dl->AddRectFilled(p.p0, p.p1, IM_COL32(18, 21, 26, 255));
    for (int i = 0; i <= hlines; i++) {
        float y = p.p0.y + (p.p1.y - p.p0.y) * float(i) / float(hlines);
        p.dl->AddLine(ImVec2(p.p0.x, y), ImVec2(p.p1.x, y), C_GRID);
        char b[32];
        float v = p.y1 - (p.y1 - p.y0) * float(i) / float(hlines);
        std::snprintf(b, sizeof b, std::fabs(p.y1 - p.y0) < 5 ? "%.1f" : "%.0f", v);
        ImVec2 ts = ImGui::CalcTextSize(b);
        if (i > 0 && i < hlines) p.dl->AddText(ImVec2(p.p0.x - ts.x - 6, y - ts.y / 2), alpha(C_DIM, 0.8f), b);
    }
    if (!label || !*label) return p;
    ImVec2 ls = ImGui::CalcTextSize(label);
    p.dl->AddRectFilled(ImVec2(p.p0.x + 4, p.p0.y + 3), ImVec2(p.p0.x + ls.x + 14, p.p0.y + ls.y + 7), alpha(C_PANEL, 0.85f), 3);
    p.dl->AddText(ImVec2(p.p0.x + 9, p.p0.y + 5), C_DIM, label);
    return p;
}

void plotLine(const Plot &p, const std::vector<ImVec2> &pts, ImU32 col, float th = 1.6f)
{
    if (pts.size() < 2) return;
    // only the visible x range (plus one point each side), at most ~2 points per pixel: a zoomed-in lap would
    // otherwise send every sample of the whole lap, far outside the plot, and overflow the draw list
    std::vector<ImVec2> s;
    s.reserve(std::min(pts.size(), size_t((p.p1.x - p.p0.x) * 2 + 8)));
    const float lo = std::min(p.x0, p.x1), hi = std::max(p.x0, p.x1);
    float lastX = -1e30f;
    for (size_t i = 0; i < pts.size(); i++) {
        const auto &q = pts[i];
        const bool in = q.x >= lo && q.x <= hi;
        const bool edge = (!in && i + 1 < pts.size() && q.x < lo && pts[i + 1].x >= lo) || (!in && i > 0 && q.x > hi && pts[i - 1].x <= hi);
        if (!in && !edge) continue;
        const ImVec2 m = p.map(q.x, std::clamp(q.y, p.y0 - (p.y1 - p.y0), p.y1 + (p.y1 - p.y0)));
        if (in && m.x - lastX < 0.5f && i + 1 < pts.size()) continue;
        s.push_back(m);
        lastX = m.x;
    }
    if (s.size() < 2) return;
    p.dl->PushClipRect(p.p0, p.p1, true);
    p.dl->AddPolyline(s.data(), int(s.size()), col, ImDrawFlags_None, th);
    p.dl->PopClipRect();
}

using Getter = float (*)(const Sample &);

// ---- sidebar / top bar / transport ---------------------------------------------------------------------------------
const char *sourceName(Source s)
{
    switch (s) {
    case Source::Live: return "Live";
    case Source::Sim: return "Simulation";
    case Source::Replay: return "Replay";
    }
    return "";
}

ImU32 sourceColour(app::App &a)
{
    if (a.source() == Source::Sim) return C_BLUE;
    if (a.source() == Source::Replay) return C_ORANGE;
    return a.secondsSinceData() < 2 ? C_GREEN : C_DIM;
}

void sidebar(app::App &a)
{
    ImGui::PushStyleColor(ImGuiCol_ChildBg, v4(C_SIDE));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::BeginChild("side", ImVec2(210, 0), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
    ImGui::PopStyleVar();
    ImDrawList *dl = ImGui::GetWindowDrawList();
    ImVec2 o = ImGui::GetCursorScreenPos();
    // logo: two slanted bars + name
    for (int i = 0; i < 2; i++) {
        float x = o.x + 20 + i * 11;
        dl->AddQuadFilled(ImVec2(x + 8, o.y + 22), ImVec2(x + 16, o.y + 22), ImVec2(x + 8, o.y + 46), ImVec2(x, o.y + 46),
                          i ? alpha(C_ACCENT, 0.55f) : C_ACCENT);
    }
    ImGui::PushFont(F->numMid);
    dl->AddText(ImVec2(o.x + 54, o.y + 16), C_TEXT, "LYTTON");
    ImGui::PopFont();
    dl->AddText(F->small, fsz(F->small), ImVec2(o.x + 56, o.y + 52), C_DIM, "Telemetry Tool v" F1RACE_VERSION);
    ImGui::Dummy(ImVec2(210, 92));

    // the order on the sidebar: the game launcher first, then the pages in their numbering
    static const int ORDER[PAGE_COUNT] = {GAME, DASH, SETUP, CAPTURE, REPLAY, COMPARE, FORWARD, HMD, SETTINGS};
    const char *groups[PAGE_COUNT] = {"RACE", nullptr, "DATA", nullptr, nullptr, "OUTPUT", nullptr, nullptr, "GAME"};
    for (int i : ORDER) {
        if (groups[i]) {
            ImGui::SetCursorPosX(22);
            ImGui::Dummy(ImVec2(0, 4));
            ImGui::SetCursorPosX(22);
            textCol(alpha(C_DIM, 0.7f), "%s", groups[i]);
        }
        ImGui::PushID(i);
        ImVec2 p = ImGui::GetCursorScreenPos();
        if (ImGui::InvisibleButton("nav", ImVec2(210, 38))) g_page = i;
        const bool sel = g_page == i, hov = ImGui::IsItemHovered();
        if (sel || hov) dl->AddRectFilled(ImVec2(p.x + 8, p.y), ImVec2(p.x + 202, p.y + 38), sel ? C_PANEL2 : alpha(C_PANEL2, 0.5f), 6);
        if (sel) dl->AddRectFilled(ImVec2(p.x + 8, p.y + 8), ImVec2(p.x + 11, p.y + 30), C_ACCENT, 2);
        // live indicators next to some entries
        if (i == DASH && a.recording() && std::fmod(ImGui::GetTime(), 1.0) < 0.6)
            dl->AddCircleFilled(ImVec2(p.x + 186, p.y + 19), 4.5f, C_RED);
        if (i == REPLAY && a.source() == Source::Replay) dl->AddCircleFilled(ImVec2(p.x + 186, p.y + 19), 4.5f, C_ORANGE);
        dl->AddText(ImVec2(p.x + 26, p.y + 9), sel ? C_TEXT : C_DIM, PAGE_NAMES[i]);
        ImGui::PopID();
    }

    // data source box at the bottom
    const float boxH = 104;
    ImGui::SetCursorPosY(ImGui::GetWindowHeight() - boxH - 12);
    ImVec2 b = ImGui::GetCursorScreenPos();
    dl->AddRectFilled(ImVec2(b.x + 10, b.y), ImVec2(b.x + 200, b.y + boxH), C_PANEL, 6);
    dl->AddRect(ImVec2(b.x + 10, b.y), ImVec2(b.x + 200, b.y + boxH), C_BORDER, 6);
    dl->AddText(ImVec2(b.x + 22, b.y + 10), C_DIM, "Source");
    const ImU32 sc = sourceColour(a);
    dl->AddCircleFilled(ImVec2(b.x + 28, b.y + 43), 5, sc);
    dl->AddText(ImVec2(b.x + 40, b.y + 33), C_TEXT, sourceName(a.source()));
    char rate[64];
    std::snprintf(rate, sizeof rate, "%.0f pkt/s", a.packetRate());
    dl->AddText(ImVec2(b.x + 22, b.y + 58), C_DIM, rate);
    if (a.recording()) {
        std::string r = (a.recordingPaused() ? "REC PAUSED " : "REC ") + app::formatDuration(a.recordingSeconds());
        dl->AddText(ImVec2(b.x + 22, b.y + 78), a.recordingPaused() ? C_YELLOW : C_RED, r.c_str());
    } else if (a.source() == Source::Live && a.secondsSinceData() > 2) {
        dl->AddText(ImVec2(b.x + 22, b.y + 78), C_DIM, "Waiting for game data...");
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
}

// The only place recording starts and stops. While recording it shows the time, the size and whether the recording
// is paused (game paused or no data).
void recordButton(app::App &a, float right, float y)
{
    if (!a.canRecord()) return; // replaying
    ImDrawList *dl = ImGui::GetWindowDrawList();
    const bool rec = a.recording(), paused = rec && a.recordingPaused();
    std::string label = !rec ? "Record"
                             : std::string(paused ? "Paused  " : "Stop  ") + app::formatDuration(a.recordingSeconds()) +
                                   "  " + app::formatBytes(a.hub.recordedBytes());
    const ImVec2 ts = ImGui::CalcTextSize(label.c_str());
    const float w = ts.x + 48, h = 30;
    const ImVec2 p0(right - w - 4, y), p1(right - 4, y + h);
    const ImVec2 back = ImGui::GetCursorScreenPos();
    ImGui::SetCursorScreenPos(p0);
    const bool click = ImGui::InvisibleButton("record", ImVec2(w, h));
    const bool hov = ImGui::IsItemHovered();
    ImGui::SetCursorScreenPos(back);
    const ImU32 col = paused ? C_YELLOW : C_RED;
    dl->AddRectFilled(p0, p1, rec ? alpha(col, hov ? 0.3f : 0.18f) : (hov ? C_ACCENT : alpha(C_ACCENT, 0.85f)), 15);
    dl->AddRect(p0, p1, rec ? col : C_ACCENT, 15, 0, 1.5f);
    const ImVec2 ic(p0.x + 20, y + h / 2);
    if (!rec) dl->AddCircleFilled(ic, 6, IM_COL32_WHITE);
    else if (paused) {
        dl->AddRectFilled(ImVec2(ic.x - 5, ic.y - 6), ImVec2(ic.x - 1.5f, ic.y + 6), col);
        dl->AddRectFilled(ImVec2(ic.x + 1.5f, ic.y - 6), ImVec2(ic.x + 5, ic.y + 6), col);
    } else {
        dl->AddRectFilled(ImVec2(ic.x - 5, ic.y - 5), ImVec2(ic.x + 5, ic.y + 5),
                          std::fmod(ImGui::GetTime(), 1.0) < 0.6 ? col : alpha(col, 0.5f), 1);
    }
    dl->AddText(ImVec2(p0.x + 34, y + (h - ts.y) / 2), C_TEXT, label.c_str());
    if (!rec && !a.recordError().empty()) { // the last start failed: say why, under the button
        const ImVec2 es = ImGui::CalcTextSize(a.recordError().c_str());
        dl->AddText(ImVec2(p1.x - es.x, p1.y + 4), C_RED, a.recordError().c_str());
    }
    if (hov)
        ImGui::SetTooltip(rec ? "Click to stop and save: %s\nPauses by itself while the game is paused or sends nothing, then carries on"
                              : "Saves the raw telemetry packets as a .f1tr file (%s)\nPauses by itself while the game is paused or sends nothing",
                          rec ? fs::path(a.recordingPath()).filename().string().c_str() : a.recordingsDir().c_str());
    if (click) {
        if (rec) a.stopRecording();
        else a.startRecording();
    }
}

void topBar(app::App &a)
{
    ImGui::BeginGroup();
    withFont(F->title, [&] { ImGui::TextUnformatted(PAGE_NAMES[g_page]); });
    textCol(C_DIM, "%s", PAGE_HINTS[g_page]);
    ImGui::EndGroup();

    // chips on the right
    ImDrawList *dl = ImGui::GetWindowDrawList();
    float right = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
    float y = ImGui::GetWindowPos().y + ImGui::GetStyle().WindowPadding.y + 8;
    auto chip = [&](const std::string &txt, ImU32 col, bool dot) {
        ImVec2 ts = ImGui::CalcTextSize(txt.c_str());
        float w = ts.x + 24 + (dot ? 14 : 0);
        ImVec2 p0(right - w, y), p1(right, y + 30);
        dl->AddRectFilled(p0, p1, alpha(col, 0.14f), 15);
        dl->AddRect(p0, p1, alpha(col, 0.5f), 15);
        if (dot) dl->AddCircleFilled(ImVec2(p0.x + 15, y + 15), 4, col);
        dl->AddText(ImVec2(p0.x + 12 + (dot ? 14 : 0), y + (30 - ts.y) / 2), C_TEXT, txt.c_str());
        right -= w + 8;
    };
    if (a.recording() && g_page != DASH)
        chip((a.recordingPaused() ? "REC PAUSED " : "REC ") + app::formatDuration(a.recordingSeconds()) + "  " +
                 app::formatBytes(a.hub.recordedBytes()),
             a.recordingPaused() ? C_YELLOW : std::fmod(ImGui::GetTime(), 1.0) < 0.6 ? C_RED : alpha(C_RED, 0.6f), true);
    char r[64];
    std::snprintf(r, sizeof r, "%.0f pkt/s", a.packetRate());
    chip(r, C_DIM, false);
    chip(sourceName(a.source()), sourceColour(a), true);
    if (g_page == DASH) recordButton(a, right, y);
    ImGui::Dummy(ImVec2(0, 6));
}

// The laps of the recording as [start, end) times; the player's lap numbers from the recording index.
struct LapSpan {
    uint64_t t0, t1;
    int lapNum;
};
std::vector<LapSpan> lapSpans(const f1hub::Replayer &rp)
{
    std::vector<LapSpan> v;
    const auto &laps = rp.laps();
    for (size_t i = 0; i < laps.size(); i++)
        v.push_back({laps[i].tNs, i + 1 < laps.size() ? laps[i + 1].tNs : rp.duration(), laps[i].lapNum});
    return v;
}

// A seek bar over [t0, t1) of the recording. Dragging previews, releasing seeks. `labelStep` = label every n-th lap
// (0 = auto); `window` marks a sub-range (the detail bar's) on the overview; `lapTimes` labels each lap with its time.
// The timeline's phase colours: what the player's car (or the session) was doing.
void phaseInfo(f1hub::Phase k, const char *&name, ImU32 &col)
{
    switch (k) {
    case f1hub::Phase::Garage: name = "Garage"; col = IM_COL32(120, 128, 140, 255); return;
    case f1hub::Phase::Pit: name = "Pit lane"; col = C_BLUE; return;
    case f1hub::Phase::OutLap: name = "Out lap"; col = C_GREEN; return;
    case f1hub::Phase::InLap: name = "In lap"; col = C_PURPLE; return;
    case f1hub::Phase::Invalid: name = "Invalid lap"; col = C_RED; return;
    case f1hub::Phase::SafetyCar: name = "Safety car"; col = C_YELLOW; return;
    case f1hub::Phase::VirtualSC: name = "Virtual safety car"; col = C_ORANGE; return;
    }
    name = "";
    col = 0;
}

void seekBar(app::App &a, const char *id, float h, uint64_t t0, uint64_t t1, bool detail,
             const std::pair<uint64_t, uint64_t> *window, const Snapshot *snap)
{
    auto &rp = a.replay;
    if (t1 <= t0) t1 = t0 + 1;
    const double span = double(t1 - t0);
    const uint64_t pos = g_dragSeek ? g_dragT : rp.position();
    ImDrawList *dl = ImGui::GetWindowDrawList();
    const ImVec2 c = ImGui::GetCursorScreenPos();
    const float w = ImGui::GetContentRegionAvail().x;
    ImGui::InvisibleButton(id, ImVec2(w, h));
    const bool active = ImGui::IsItemActive(), hov = ImGui::IsItemHovered();
    const float mx = std::clamp((ImGui::GetIO().MousePos.x - c.x) / w, 0.f, 1.f);
    const uint64_t mt = t0 + uint64_t(double(mx) * span);
    if (active) { g_dragSeek = true; g_dragT = mt; }
    if (ImGui::IsItemDeactivated() && g_dragSeek) {
        rp.seek(g_dragT);
        g_dragSeek = false;
    }
    auto X = [&](uint64_t t) { return c.x + w * float(std::clamp((double(t) - double(t0)) / span, 0.0, 1.0)); };
    const float cy = detail ? c.y + h - 14 : c.y + h / 2;
    const auto laps = lapSpans(rp);

    if (detail) {
        // one block per lap, alternating shade, lap number and time above
        for (size_t i = 0; i < laps.size(); i++) {
            const auto &l = laps[i];
            if (l.t1 <= t0 || l.t0 >= t1) continue;
            const float x0 = X(l.t0), x1 = X(l.t1);
            dl->AddRectFilled(ImVec2(x0, cy - 5), ImVec2(x1, cy + 5), l.lapNum % 2 ? C_PANEL2 : IM_COL32(38, 44, 54, 255));
            if (pos > l.t0) dl->AddRectFilled(ImVec2(x0, cy - 5), ImVec2(std::min(x1, X(pos)), cy + 5), alpha(C_ORANGE, l.lapNum % 2 ? 0.95f : 0.8f));
            dl->AddLine(ImVec2(x0, cy - 22), ImVec2(x0, cy + 8), alpha(C_TEXT, 0.4f));
            char b[48];
            std::string lt;
            if (snap && snap->playerIdx < f1tel::MAX_CARS) {
                const auto &me = snap->cars[snap->playerIdx];
                if (l.lapNum >= 1 && size_t(l.lapNum) <= me.laps.size() && me.laps[l.lapNum - 1].lapMs)
                    lt = formatLap(me.laps[l.lapNum - 1].lapMs);
            }
            std::snprintf(b, sizeof b, "L%d", l.lapNum);
            dl->PushClipRect(ImVec2(x0, c.y), ImVec2(x1, c.y + h), true);
            dl->AddText(ImVec2(x0 + 6, c.y), pos >= l.t0 && pos < l.t1 ? C_TEXT : C_DIM, b);
            if (!lt.empty()) dl->AddText(F->num, fsz(F->num) * 0.85f, ImVec2(x0 + 6 + ImGui::CalcTextSize(b).x + 8, c.y + 1), alpha(C_DIM, 0.9f), lt.c_str());
            dl->PopClipRect();
        }
        if (laps.empty()) {
            dl->AddRectFilled(ImVec2(c.x, cy - 5), ImVec2(c.x + w, cy + 5), C_PANEL2, 3);
            dl->AddRectFilled(ImVec2(c.x, cy - 5), ImVec2(X(pos), cy + 5), C_ORANGE, 3);
        }
        // times at both ends
        const std::string a0 = app::formatDuration(double(t0) / 1e9), a1 = app::formatDuration(double(t1) / 1e9);
        dl->AddText(ImVec2(c.x, cy + 7), alpha(C_DIM, 0.8f), a0.c_str());
        dl->AddText(ImVec2(c.x + w - ImGui::CalcTextSize(a1.c_str()).x, cy + 7), alpha(C_DIM, 0.8f), a1.c_str());
    } else {
        dl->AddRectFilled(ImVec2(c.x, cy - 3), ImVec2(c.x + w, cy + 3), C_PANEL2, 3);
        dl->AddRectFilled(ImVec2(c.x, cy - 3), ImVec2(X(pos), cy + 3), C_ORANGE, 3);
        if (window) { // the detail bar's range
            const float wx0 = X(window->first), wx1 = X(window->second);
            dl->AddRectFilled(ImVec2(wx0, cy - 9), ImVec2(wx1, cy + 9), alpha(C_TEXT, 0.10f), 3);
            dl->AddRect(ImVec2(wx0, cy - 9), ImVec2(wx1, cy + 9), alpha(C_TEXT, 0.45f), 3);
        }
        const int step = laps.size() > 60 ? 10 : laps.size() > 25 ? 5 : laps.size() > 12 ? 2 : 1;
        for (const auto &l : laps) {
            const float x = X(l.t0);
            const bool label = h > 30 && l.lapNum % step == 0;
            dl->AddLine(ImVec2(x, cy - (label ? 7 : 4)), ImVec2(x, cy + (label ? 7 : 4)), alpha(C_TEXT, label ? 0.45f : 0.22f));
            if (label) {
                char b[16];
                std::snprintf(b, sizeof b, "L%d", l.lapNum);
                dl->AddText(ImVec2(x + 3, cy + 6), C_DIM, b);
            }
        }
    }
    // phases in colour under the bar: pit lane, out / in laps, invalid laps; safety cars over it
    std::string here;
    for (const auto &ph : rp.phases()) {
        if (ph.t1 <= t0 || ph.t0 >= t1) continue;
        const char *name;
        ImU32 col;
        phaseInfo(ph.kind, name, col);
        const bool sc = ph.kind == f1hub::Phase::SafetyCar || ph.kind == f1hub::Phase::VirtualSC;
        const float y0 = sc ? cy - (detail ? 10 : 8) : cy + (detail ? 6 : 4), y1 = y0 + 3;
        dl->AddRectFilled(ImVec2(X(ph.t0), y0), ImVec2(std::max(X(ph.t1), X(ph.t0) + 2), y1), col);
        if (hov && mt >= ph.t0 && mt < ph.t1) here += std::string("  ·  ") + name;
    }
    dl->AddCircleFilled(ImVec2(X(pos), cy), active ? 8.f : 6.5f, IM_COL32_WHITE);
    if (hov) {
        int lap = 0;
        for (const auto &l : laps)
            if (mt >= l.t0) lap = l.lapNum;
        if (lap) ImGui::SetTooltip("%s  Lap %d%s", app::formatDuration(double(mt) / 1e9).c_str(), lap, here.c_str());
        else ImGui::SetTooltip("%s%s", app::formatDuration(double(mt) / 1e9).c_str(), here.c_str());
    }
}

// The detail bar's range: `n` laps around the current one (two before it where possible), or 6 minutes without laps.
std::pair<uint64_t, uint64_t> detailWindow(const f1hub::Replayer &rp, int n)
{
    const uint64_t pos = rp.position(), dur = rp.duration();
    const auto laps = lapSpans(rp);
    if (laps.empty()) {
        const uint64_t half = 180ull * 1000000000ull;
        uint64_t t0 = pos > half ? pos - half : 0;
        uint64_t t1 = std::min(dur, t0 + 2 * half);
        return {t1 > 2 * half ? t1 - 2 * half : 0, t1};
    }
    int k = 0;
    for (int i = 0; i < int(laps.size()); i++)
        if (pos >= laps[i].t0) k = i;
    int first = std::max(0, k - (n - 1) / 2 - ((n - 1) % 2));
    first = std::min(first, std::max(0, int(laps.size()) - n));
    const int last = std::min(int(laps.size()) - 1, first + n - 1);
    const uint64_t t0 = first == 0 ? 0 : laps[first].t0;
    return {t0, laps[last].t1};
}

// "12:30", "1:05:00", "90", "12:30.5" -> seconds
bool parseTime(const char *text, double &out)
{
    double parts[3] = {0, 0, 0};
    int n = 0;
    const char *p = text;
    while (*p && n < 3) {
        char *end = nullptr;
        const double v = std::strtod(p, &end);
        if (end == p) return false;
        parts[n++] = v;
        p = end;
        if (*p == ':' || *p == '\xEF') { // ASCII colon or a full-width one (UTF-8 EF BC 9A)
            p += *p == ':' ? 1 : 3;
        } else if (*p) {
            return false;
        }
    }
    if (n == 0 || *p) return false;
    out = n == 1 ? parts[0] : n == 2 ? parts[0] * 60 + parts[1] : parts[0] * 3600 + parts[1] * 60 + parts[2];
    return true;
}

void transportControls(app::App &a, bool big)
{
    auto &rp = a.replay;
    const float bh = big ? 40.f : 30.f;
    const uint64_t ten = 10'000'000'000ull;
    const float y0 = ImGui::GetCursorPosY();
    if (ImGui::Button("-10s", ImVec2(big ? 64 : 52, bh))) rp.seek(rp.position() > ten ? rp.position() - ten : 0);
    ImGui::SameLine();
    if (accentButton(rp.playing() ? "Pause" : "Play", ImVec2(big ? 96 : 72, bh), C_ORANGE)) {
        if (rp.playing()) rp.pause();
        else rp.play();
    }
    ImGui::SameLine();
    if (ImGui::Button("+10s", ImVec2(big ? 64 : 52, bh))) rp.seek(rp.position() + ten);
    ImGui::SameLine();
    const double pos = double(g_dragSeek ? g_dragT : rp.position()) / 1e9;
    // current time: click it to type a time and jump there
    static int editing = -1; // which control (big / small) is being edited
    static char buf[32] = "";
    const int me = big ? 1 : 0;
    const std::string total = app::formatDuration(double(rp.duration()) / 1e9);
    if (editing == me) {
        ImGui::SetCursorPosY(y0 + (bh - ImGui::GetFrameHeight()) / 2);
        ImGui::SetNextItemWidth(big ? 110.f : 96.f);
        if (ImGui::IsWindowAppearing() || !ImGui::IsAnyItemActive()) ImGui::SetKeyboardFocusHere();
        ImGui::PushID(me);
        const bool enter = ImGui::InputTextWithHint("##goto", "m:ss", buf, sizeof buf,
                                                    ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll |
                                                        ImGuiInputTextFlags_CharsNoBlank);
        // Enter or leaving the box applies the time (with a Chinese IME the Enter key may only commit the text);
        // Esc cancels
        const bool escape = ImGui::IsKeyPressed(ImGuiKey_Escape);
        const bool done = enter || ImGui::IsItemDeactivated();
        ImGui::PopID();
        if (escape) {
            editing = -1;
        } else if (done) {
            double t;
            if (parseTime(buf, t)) rp.seek(uint64_t(std::max(0.0, t) * 1e9));
            editing = -1;
        }
        ImGui::SameLine(0, 6);
        ImGui::SetCursorPosY(y0 + (bh - fsz(F->num)) / 2);
        withFont(F->num, [&] { textCol(C_DIM, "/ %s", total.c_str()); });
    } else {
        ImGui::SetCursorPosY(y0 + (bh - fsz(F->num)) / 2);
        const std::string cur = app::formatDuration(pos);
        ImGui::PushFont(F->num);
        const ImVec2 cs = ImGui::CalcTextSize(cur.c_str());
        const ImVec2 cp = ImGui::GetCursorScreenPos();
        ImGui::PushID(me);
        const bool click = ImGui::InvisibleButton("time", cs);
        ImGui::PopID();
        const bool hov = ImGui::IsItemHovered();
        ImDrawList *dl = ImGui::GetWindowDrawList();
        if (hov) dl->AddRectFilled(ImVec2(cp.x - 4, cp.y - 2), ImVec2(cp.x + cs.x + 4, cp.y + cs.y + 2), C_PANEL2, 4);
        dl->AddText(cp, C_TEXT, cur.c_str());
        dl->AddLine(ImVec2(cp.x, cp.y + cs.y + 1), ImVec2(cp.x + cs.x, cp.y + cs.y + 1), alpha(C_TEXT, hov ? 0.8f : 0.3f));
        ImGui::SameLine(0, 6);
        ImGui::SetCursorScreenPos(ImVec2(ImGui::GetCursorScreenPos().x, cp.y));
        textCol(C_DIM, "/ %s", total.c_str());
        ImGui::PopFont();
        if (hov) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_TextInput);
            ImGui::SetTooltip("Click to jump to a time, e.g. 12:30, 1:05:00 or 90 (seconds); Enter to go, Esc to cancel");
        }
        if (click) {
            editing = me;
            std::snprintf(buf, sizeof buf, "%s", cur.c_str());
        }
    }
    ImGui::SameLine();
    static const float speeds[] = {0.25f, 0.5f, 1.f, 2.f, 4.f, 8.f, 16.f};
    static const char *names[] = {"0.25x", "0.5x", "1x", "2x", "4x", "8x", "16x"};
    int cur = 2;
    for (int i = 0; i < 7; i++)
        if (std::fabs(speeds[i] - rp.speed()) < 1e-3) cur = i;
    ImGui::SetNextItemWidth(84);
    ImGui::SetCursorPosY(y0 + (bh - ImGui::GetFrameHeight()) / 2);
    if (ImGui::Combo("##speed", &cur, names, 7)) rp.setSpeed(speeds[cur]);
}

void bottomTransport(app::App &a)
{
    ImGui::PushStyleColor(ImGuiCol_ChildBg, v4(C_SIDE));
    ImGui::BeginChild("transport", ImVec2(0, 58), ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar);
    ImGui::PopStyleColor();
    ImGui::SetCursorPosY(12);
    transportControls(a, false);
    ImGui::SameLine(0, 20);
    ImGui::SetCursorPosY(12);
    seekBar(a, "seek", 30, 0, a.replay.duration(), false, nullptr, nullptr);
    ImGui::EndChild();
}

// ---- empty state ---------------------------------------------------------------------------------------------------
bool emptyState(app::App &a, const Snapshot &s)
{
    if (s.hasData) return false;
    ImVec2 avail = ImGui::GetContentRegionAvail();
    ImGui::SetCursorPos(ImVec2(ImGui::GetCursorPosX() + avail.x / 2 - 230, ImGui::GetCursorPosY() + avail.y / 2 - 110));
    beginCard("empty", ImVec2(460, 210));
    withFont(F->title, [] { ImGui::TextUnformatted("No telemetry yet"); });
    ImGui::Spacing();
    if (a.source() == Source::Live)
        ImGui::TextWrapped("Listening on UDP %u. In F1 25's telemetry settings turn UDP on: IP 127.0.0.1, port %u, format 2025.",
                           a.cfg.port, a.cfg.port);
    else
        ImGui::TextWrapped("%s", a.status().c_str());
    ImGui::Spacing();
    ImGui::TextWrapped("No game on this PC? The simulation shows every feature.");
    ImGui::Spacing();
    if (accentButton("Start Simulation", ImVec2(150, 34), C_BLUE)) a.useSim();
    ImGui::SameLine();
    if (ImGui::Button("Open Recording...", ImVec2(120, 34))) {
        std::string p = pickRecording(a.recordingsDir());
        if (!p.empty()) a.useReplay(p);
    }
    endCard();
    return true;
}

// ---- dashboard -----------------------------------------------------------------------------------------------------
// Names in the standings: three-letter codes (driverCode: also for the game's Chinese names, which the desktop font
// cannot draw in full); yours as set on the settings page; another human's from the last Latin word of the name
// (playerCode: "Lyton·Yao" -> LTN), else PLAYER.
std::string displayName(const Car &c)
{
    return c.ai || c.you ? driverCode(c) : playerCode(c.name);
}

std::string gapText(const Snapshot &s, int i, int mode)
{
    const auto &c = s.cars[i];
    if (rankedOnBestLap(s)) { // practice / qualifying: best-lap differences
        if (!c.bestLapMs) return "NO TIME";
        if (mode != 0 && c.position == 1) return "LEADER";
        const int ref = mode == 0 ? int(s.playerIdx) : mode == 2 ? s.order().front() : carAhead(s, i);
        const auto g = bestLapGap(s, i, ref);
        if (!g.valid) return "";
        return mode == 0 ? formatSignedGap(g.ms) : formatGap(uint32_t(std::max(0, g.ms)));
    }
    if (mode == 1) return c.position == 1 ? "LEADER" : formatGap(c.gapToCarAheadMs);
    if (mode == 2) return c.position == 1 ? "LEADER" : formatGap(c.gapToLeaderMs);
    auto g = relativeToPlayer(s, i);
    if (!g.valid) return "";
    if (g.laps) {
        char b[16];
        std::snprintf(b, sizeof b, "%+d LAP", g.laps);
        return b;
    }
    return formatSignedGap(g.ms);
}

void carCard(const Snapshot &s, float w, float h)
{
    const auto &p = s.player;
    beginCard("car", ImVec2(w, h), "CAR");
    ImDrawList *dl = ImGui::GetWindowDrawList();
    // speed + gear
    ImVec2 c = ImGui::GetCursorScreenPos();
    char b[32];
    std::snprintf(b, sizeof b, "%u", unsigned(p.speed));
    ImGui::PushFont(F->numBig);
    dl->AddText(c, C_TEXT, b);
    const float sw = ImGui::CalcTextSize("000").x;
    ImGui::PopFont();
    dl->AddText(ImVec2(c.x + sw + 8, c.y + 40), C_DIM, "km/h");
    const char *g = p.gear < 0 ? "R" : p.gear == 0 ? "N" : nullptr;
    std::snprintf(b, sizeof b, "%d", p.gear);
    const float gx = c.x + ImGui::GetContentRegionAvail().x - 70;
    dl->AddRectFilled(ImVec2(gx, c.y), ImVec2(gx + 70, c.y + 66), C_PANEL2, 6);
    ImGui::PushFont(F->numBig);
    ImVec2 gs = ImGui::CalcTextSize(g ? g : b);
    dl->AddText(ImVec2(gx + (70 - gs.x) / 2, c.y + (66 - gs.y) / 2), C_TEXT, g ? g : b);
    ImGui::PopFont();
    ImGui::Dummy(ImVec2(0, 70));

    // rev LEDs + rpm
    {
        ImVec2 q = ImGui::GetCursorScreenPos();
        float aw = ImGui::GetContentRegionAvail().x;
        const int N = 15;
        float lw = (aw - (N - 1) * 4) / N;
        int lit = int(std::round(p.revLights / 100.f * N));
        for (int i = 0; i < N; i++) {
            ImU32 col = i < 5 ? C_GREEN : i < 10 ? C_RED : C_PURPLE;
            float x = q.x + i * (lw + 4);
            dl->AddRectFilled(ImVec2(x, q.y), ImVec2(x + lw, q.y + 8), i < lit ? col : alpha(col, 0.15f), 3);
        }
        ImGui::Dummy(ImVec2(aw, 10));
        withFont(F->num, [&] { textCol(C_DIM, "%u rpm", unsigned(p.rpm)); });
        ImGui::SameLine();
        // DRS chip at the right end of the rpm line
        ImVec2 r = ImGui::GetCursorScreenPos();
        const float rx = q.x + aw - 52;
        const bool open = p.drs, allowed = p.drsAllowed, fault = s.damage.drsFault;
        ImU32 dc = fault ? C_RED : open ? C_GREEN : allowed ? C_YELLOW : C_DIM;
        dl->AddRectFilled(ImVec2(rx, r.y), ImVec2(rx + 52, r.y + 22), open ? alpha(dc, 0.25f) : 0, 4);
        dl->AddRect(ImVec2(rx, r.y), ImVec2(rx + 52, r.y + 22), dc, 4);
        dl->AddText(ImVec2(rx + 11, r.y + 2), dc, "DRS");
        ImGui::NewLine();
    }
    char lb[48];
    std::snprintf(lb, sizeof lb, "THR %3.0f%%", p.throttle * 100);
    hbar(p.throttle, alpha(C_GREEN, 0.85f), 20, lb);
    std::snprintf(lb, sizeof lb, "BRK %3.0f%%", p.brake * 100);
    hbar(p.brake, alpha(C_RED, 0.85f), 20, lb);
    // steering + ERS on one line
    {
        ImVec2 q = ImGui::GetCursorScreenPos();
        float aw = ImGui::GetContentRegionAvail().x, half = (aw - 10) / 2;
        dl->AddRectFilled(q, ImVec2(q.x + half, q.y + 20), C_PANEL2, 3);
        float mid = q.x + half / 2, x = mid + p.steer * half / 2;
        dl->AddRectFilled(ImVec2(std::min(mid, x), q.y), ImVec2(std::max(mid, x), q.y + 20), alpha(C_CYAN, 0.55f), 3);
        dl->AddLine(ImVec2(mid, q.y), ImVec2(mid, q.y + 20), C_DIM);
        dl->AddText(ImVec2(q.x + 6, q.y + 1), C_TEXT, "STEER");
        const float ers = p.ersJ / 4.0e6f;
        const float ex = q.x + half + 10;
        dl->AddRectFilled(ImVec2(ex, q.y), ImVec2(ex + half, q.y + 20), C_PANEL2, 3);
        dl->AddRectFilled(ImVec2(ex, q.y), ImVec2(ex + half * std::clamp(ers, 0.f, 1.f), q.y + 20),
                          s.damage.ersFault ? alpha(C_RED, 0.7f) : alpha(C_YELLOW, 0.75f), 3);
        std::snprintf(lb, sizeof lb, s.damage.ersFault ? "ERS FAULT" : "ERS %.0f%%", ers * 100);
        dl->AddText(ImVec2(ex + 6, q.y + 1), C_TEXT, lb);
        ImGui::Dummy(ImVec2(aw, 22));
    }

    // fuel: the game sends the tank level only; flow and per-lap use are worked out from it
    ImGui::Spacing();
    const auto &f = s.fuel;
    {
        ImVec2 q = ImGui::GetCursorScreenPos();
        float aw = ImGui::GetContentRegionAvail().x;
        dl->AddLine(ImVec2(q.x, q.y), ImVec2(q.x + aw, q.y), C_BORDER);
        ImGui::Dummy(ImVec2(aw, 4));
    }
    textCol(C_DIM, "FUEL");
    ImGui::SameLine();
    withFont(F->num, [&] { ImGui::Text("%.2f kg", p.fuelKg); });
    ImGui::SameLine();
    textCol(p.fuelLaps < 0 ? C_RED : C_GREEN, "%+.1f LAPS", p.fuelLaps);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("The game's fuel margin: laps more (+) or fewer (-) than the race needs");
    {
        const float aw = ImGui::GetContentRegionAvail().x, cw = (aw - 16) / 3;
        auto cell = [&](int i, const char *label, const std::string &v, ImU32 col) {
            if (i) ImGui::SameLine(0, 8);
            ImGui::BeginGroup();
            textCol(C_DIM, "%s", label);
            withFont(F->num, [&] { textCol(col, "%s", v.c_str()); });
            ImGui::EndGroup();
            ImGui::SameLine(0, 0);
            ImGui::Dummy(ImVec2(std::max(0.f, cw - ImGui::GetItemRectSize().x), 0));
        };
        char v[48];
        std::snprintf(v, sizeof v, "%.1f g/s", f.kgPerS * 1000);
        cell(0, "NOW", v, C_TEXT);
        std::snprintf(v, sizeof v, f.kgPer100km > 0 ? "%.0f kg" : "-", f.kgPer100km);
        cell(1, "PER 100 KM", v, C_TEXT);
        std::snprintf(v, sizeof v, "%.2f kg", f.lapUsedKg);
        cell(2, "THIS LAP", v, C_TEXT);
        ImGui::NewLine();
        std::snprintf(v, sizeof v, f.lastLapKg > 0 ? "%.2f kg" : "-", f.lastLapKg);
        cell(0, "LAST LAP", v, C_TEXT);
        std::snprintf(v, sizeof v, f.avgLapKg > 0 ? "%.2f kg" : "-", f.avgLapKg);
        cell(1, "AVG / LAP", v, C_TEXT);
        std::snprintf(v, sizeof v, f.avgLapKg > 0 ? "%.1f" : "-", f.avgLapKg > 0 ? p.fuelKg / f.avgLapKg : 0.f);
        cell(2, "LAPS LEFT", v, C_GREEN);
        ImGui::NewLine();
    }
    endCard();
}

ImU32 damageColour(float pct)
{
    if (pct < 1) return alpha(C_GREEN, 0.75f);
    if (pct < 20) return C_YELLOW;
    if (pct < 50) return C_ORANGE;
    return C_RED;
}

const char *surfaceName(uint8_t s)
{
    static const char *n[] = {"Tarmac", "Kerb", "Concrete", "Rock", "Gravel", "Mud", "Sand", "Grass", "Water", "Cobbles", "Metal", "Ridged"};
    return s < 12 ? n[s] : "?";
}

ImU32 surfaceColour(uint8_t s)
{
    if (s == uint8_t(f1tel::Surface::RumbleStrip)) return C_YELLOW;
    if (s >= uint8_t(f1tel::Surface::Rock) && s <= uint8_t(f1tel::Surface::Water)) return C_RED;
    return C_DIM;
}

// Tyres and car condition in one card: a top view of the car with the four tyres on it (colour = inner
// (carcass) temperature as the wheel shows it, centre strip = surface temperature, outline = surface under the wheel) and the aero parts coloured by
// damage; each tyre's numbers beside it; power unit wear, faults and recent changes on the right.
void carStatusCard(const Snapshot &s, float w, float h)
{
    const auto &p = s.player;
    const auto &wf = s.wheels;
    const auto &dm = s.damage;
    beginCard("carstatus", ImVec2(w, h), nullptr);
    ImDrawList *dl = ImGui::GetWindowDrawList();
    textCol(C_DIM, "TYRES & CONDITION");
    ImGui::PushFont(F->small); // a dense card: everything below in the small font

    // state chips on the title line: DRS (fault / open / available / closed), ERS (fault / deploy mode), engine failures
    {
        struct Chip {
            std::string name;
            ImU32 col;
            bool filled; // active or failed: filled background
            bool show;
        };
        const char *ersModes[4] = {"NONE", "MEDIUM", "HOTLAP", "OVERTAKE"};
        const Chip chips[] = {
            dm.drsFault ? Chip{"DRS FAULT", C_RED, true, true}
            : p.drs     ? Chip{"DRS OPEN", C_GREEN, true, true}
            : p.drsAllowed ? Chip{"DRS READY", C_YELLOW, false, true}
                           : Chip{"DRS OFF", C_DIM, false, true},
            dm.ersFault ? Chip{"ERS FAULT", C_RED, true, true}
                        : Chip{std::string("ERS ") + (p.ersMode < 4 ? ersModes[p.ersMode] : "?"),
                               p.ersMode == 3 ? C_PURPLE : p.ersMode == 2 ? C_ORANGE : p.ersMode == 1 ? C_CYAN : C_DIM,
                               p.ersMode >= 2, true},
            {"ENGINE BLOWN", C_RED, true, dm.engineBlown},
            {"ENGINE SEIZED", C_RED, true, dm.engineSeized},
        };
        float x = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
        const float y = ImGui::GetCursorScreenPos().y - fsz(F->text) - ImGui::GetStyle().ItemSpacing.y;
        for (int k = 3; k >= 0; k--) {
            const auto &c = chips[k];
            if (!c.show) continue; // engine failures only when they happen
            ImVec2 ts = ImGui::CalcTextSize(c.name.c_str());
            const float cw = ts.x + 16;
            x -= cw;
            dl->AddRectFilled(ImVec2(x, y), ImVec2(x + cw, y + 18), alpha(c.col, c.filled ? 0.28f : 0.08f), 9);
            dl->AddRect(ImVec2(x, y), ImVec2(x + cw, y + 18), c.col, 9);
            dl->AddText(ImVec2(x + 8, y + 1), c.filled ? C_TEXT : c.col, c.name.c_str());
            x -= 6;
        }
    }

    const ImVec2 o = ImGui::GetCursorScreenPos();
    const ImVec2 av = ImGui::GetContentRegionAvail();
    const float bodyH = av.y;
    const float puW = std::max(180.f, av.x * 0.28f);
    const float carW = std::min(130.f, bodyH * 0.42f);
    const float callW = std::max(120.f, (av.x - puW - carW - 24) / 2);
    const float carX = o.x + callW + 6;
    const float carH = bodyH - 4, y0 = o.y + 2;

    const uint8_t compound = s.playerIdx < f1tel::MAX_CARS ? s.cars[s.playerIdx].visualTyre : 0;
    auto tempColour = [compound](float t) { return tyrecol::temp(t, compound); };
    // ---- the car: Track N Race's wireframe, damage zones and tyres filled ----
    const carwire::Placement cp = carwire::fit(ImVec2(carX, y0), ImVec2(carW, carH));
    carwire::Colours cc;
    cc.bg = C_PANEL;
    if (dm.valid) {
        auto dmg = [&](float pct) { return alpha(damageColour(pct), 0.55f); };
        cc.fill[carwire::FW_L] = dmg(dm.frontWingL);
        cc.fill[carwire::FW_R] = dmg(dm.frontWingR);
        cc.fill[carwire::FLOOR] = dmg(dm.floor);
        cc.fill[carwire::DIFF] = dmg(dm.diffuser);
        cc.fill[carwire::SP_L] = cc.fill[carwire::SP_R] = dmg(dm.sidepod);
        cc.fill[carwire::RW] = dmg(dm.rearWing);
    }
    // tyres: FL FR RL RR (the game's order is RL RR FL FR), filled with the inner (carcass) temperature
    const int idx[4] = {2, 3, 0, 1};
    const char *names[4] = {"FL", "FR", "RL", "RR"};
    const carwire::Part tyrePart[4] = {carwire::T_FL, carwire::T_FR, carwire::T_RL, carwire::T_RR};
    for (int k = 0; k < 4; k++) cc.fill[tyrePart[k]] = alpha(tempColour(p.tyreInner[idx[k]]), 0.85f);
    carwire::draw(dl, cp, cc);
    ImVec2 wheelMid[4];
    for (int k = 0; k < 4; k++) {
        const int i = idx[k];
        const bool right = k % 2 == 1;
        ImVec2 a, b;
        carwire::box(tyrePart[k], cp, a, b);
        const float t0 = a.y, t1 = b.y;
        // the surface under it
        const uint8_t sf = p.surface[i];
        if (sf != 0 && sf != uint8_t(f1tel::Surface::Concrete))
            dl->AddRect(ImVec2(a.x - 3, a.y - 3), ImVec2(b.x + 3, b.y + 3), surfaceColour(sf), 7, 0, 2.5f);
        wheelMid[k] = ImVec2(right ? b.x : a.x, (t0 + t1) / 2);
    }
    // where the car's weight sits: the centre of the four wheel loads, between the axles front to back and between
    // the wheels side to side, so the static balance (setup, fuel) and the load moving under braking, acceleration
    // and cornering both show
    if (wf.valid) {
        float sum = 0, x = 0, y = 0;
        for (int k = 0; k < 4; k++) {
            ImVec2 ta, tb;
            carwire::box(tyrePart[k], cp, ta, tb);
            const float l = std::max(0.f, wf.load[idx[k]]);
            x += l * (ta.x + tb.x) / 2;
            y += l * (ta.y + tb.y) / 2;
            sum += l;
        }
        if (sum > 1.f) {
            const ImVec2 c(x / sum, y / sum);
            dl->AddCircleFilled(c, 6.f, IM_COL32(13, 15, 19, 200));
            dl->AddCircleFilled(c, 4.5f, C_CYAN);
        }
    }
    ImGui::SetCursorScreenPos(ImVec2(carX, o.y));
    ImGui::InvisibleButton("car", ImVec2(carW, bodyH)); // no tooltip over the car (user's choice)

    // ---- callouts beside the car ----
    const float lh = ImGui::GetTextLineHeight();
    for (int k = 0; k < 4; k++) {
        const int i = idx[k];
        const bool right = k % 2 == 1, rear = k >= 2;
        const float bx = right ? carX + carW + 12 : o.x;
        const float by = rear ? o.y + bodyH / 2 + 4 : o.y;
        const float bh = bodyH / 2 - 6;
        dl->AddRectFilled(ImVec2(bx, by), ImVec2(bx + callW, by + bh), alpha(C_PANEL2, 0.7f), 6);
        // leader line to the wheel
        const ImVec2 anchor(right ? bx : bx + callW, by + 18);
        dl->AddLine(anchor, wheelMid[k], alpha(C_DIM, 0.45f), 1.f);
        dl->PushClipRect(ImVec2(bx, by), ImVec2(bx + callW, by + bh), true);
        float y = by + 5;
        const float x = bx + 9;
        char b[64];
        dl->AddText(ImVec2(x, y), C_DIM, names[k]);
        {
            const uint8_t sf = p.surface[i];
            const char *sn = surfaceName(sf);
            ImVec2 ts = ImGui::CalcTextSize(sn);
            dl->AddText(ImVec2(bx + callW - ts.x - 8, y), sf == 0 ? alpha(C_DIM, 0.7f) : surfaceColour(sf), sn);
        }
        y += lh + 2;
        std::snprintf(b, sizeof b, "%u°", unsigned(p.tyreInner[i]));
        dl->AddText(F->numMid, fsz(F->numMid) * 0.7f, ImVec2(x, y), tempColour(p.tyreInner[i]), b);
        y += fsz(F->numMid) * 0.7f;
        auto row = [&](const char *label, const char *value, ImU32 col) {
            dl->AddText(ImVec2(x, y), C_DIM, label);
            ImVec2 vs = ImGui::CalcTextSize(value);
            dl->AddText(ImVec2(bx + callW - vs.x - 8, y), col, value);
            y += lh - 1;
        };
        if (wf.valid) {
            std::snprintf(b, sizeof b, "%.2f kN", wf.load[i] / 1000.f);
            row("Load", b, C_TEXT);
            const float sr = wf.slipRatio[i] * 100.f;
            std::snprintf(b, sizeof b, "%+.1f%%", sr);
            row("Slip ratio", b, std::fabs(sr) > 10.f ? C_ORANGE : C_TEXT);
            std::snprintf(b, sizeof b, "%+.1f°", wf.slipAngle[i] * 57.2958f);
            row("Slip angle", b, C_TEXT);
        }
        std::snprintf(b, sizeof b, "%.1f psi", p.tyrePressure[i]);
        row("Pressure", b, C_TEXT);
        std::snprintf(b, sizeof b, "%u°", unsigned(p.brakeTemp[i]));
        row("Brake", b, C_TEXT);
        // wear, coloured as a warning: a smooth hue from green (new) through yellow to red (75 % and more)
        {
            const float w = p.tyreWear[i];
            const ImU32 wc = tyrecol::wear(w);
            std::snprintf(b, sizeof b, "%.0f%%", w);
            dl->AddText(ImVec2(x, y + 2), C_DIM, "WEAR");
            const float fz = fsz(F->numMid) * 0.7f;
            const ImVec2 vs = F->numMid->CalcTextSizeA(fz, FLT_MAX, 0, b);
            dl->AddText(F->numMid, fz, ImVec2(bx + callW - vs.x - 8, y - 2), wc, b);
        }
        dl->PopClipRect();
        ImGui::SetCursorScreenPos(ImVec2(bx, by));
        ImGui::PushID(k);
        ImGui::InvisibleButton("tyre", ImVec2(callW, bh));
        ImGui::PopID();
        if (ImGui::IsItemHovered() && wf.valid)
            ImGui::SetTooltip("%s (raw game values)\nLoad %.0f N\nLateral force %+.0f N\nLongitudinal force %+.0f N\nSlip ratio %+.3f\nSlip angle %+.2f°\nCarcass damage %u%%  Blisters %u%%",
                              names[k], wf.load[i], wf.latForce[i], wf.longForce[i], wf.slipRatio[i],
                              wf.slipAngle[i] * 57.2958f, unsigned(dm.tyreDamage[i]), unsigned(dm.blisters[i]));
    }

    // ---- power unit, gearbox, engine; recent changes ----
    const float px = o.x + av.x - puW;
    dl->AddLine(ImVec2(px - 8, o.y), ImVec2(px - 8, o.y + bodyH), C_BORDER);
    ImGui::SetCursorScreenPos(ImVec2(px, o.y));
    ImGui::BeginGroup();
    // chassis numbers: G forces (the target is on the car), centre of gravity, ride heights, pitch and roll
    {
        const auto &ch = s.chassis;
        const ImVec2 q = ImGui::GetCursorScreenPos();
        ImGui::BeginGroup();
        char b[64];
        auto line = [&](const char *label, const char *value) {
            // the value in the card's font, right-aligned; on its own line under the label when both don't fit
            const ImVec2 r = ImGui::GetCursorScreenPos();
            const float lh = ImGui::GetTextLineHeight();
            dl->AddText(r, C_DIM, label);
            const float lw = ImGui::CalcTextSize(label).x, vw = ImGui::CalcTextSize(value).x;
            const bool wrap = lw + vw + 10 > puW;
            dl->AddText(ImVec2(r.x + puW - vw, wrap ? r.y + lh : r.y), C_TEXT, value);
            ImGui::Dummy(ImVec2(puW, (wrap ? 2 * lh : lh) + 1));
        };
        if (ch.valid) {
            std::snprintf(b, sizeof b, "%+.2f  %+.2f  %+.2f", ch.gLat, ch.gLong, ch.gVert);
            line("G LAT/LON/VERT", b);
        } else {
            textCol(alpha(C_DIM, 0.7f), "No Motion data yet");
        }
        if (ch.exValid) {
            std::snprintf(b, sizeof b, "%.0f mm", ch.cogHeight * 1000.f);
            line("CoG height", b);
            std::snprintf(b, sizeof b, "%.0f / %.0f mm", ch.frontAeroHeight * 1000.f, ch.rearAeroHeight * 1000.f);
            line("Ride F/R", b);
            std::snprintf(b, sizeof b, "%+.2f / %+.2f", ch.chassisPitch * 57.2958f, ch.frontRoll * 57.2958f);
            line("Pitch/Roll °", b);
        }
        ImGui::EndGroup();
        ImGui::SetCursorScreenPos(ImVec2(q.x, ImGui::GetItemRectMax().y + 4));
        if (ImGui::IsItemHovered() && ch.exValid)
            ImGui::SetTooltip("G (Motion): lateral %+.2f  longitudinal %+.2f  vertical %+.2f\n"
                              "Local velocity x %+.2f  y %+.2f  z %+.2f m/s\nAngular velocity x %+.2f  y %+.2f  z %+.2f rad/s\n"
                              "Front wheel angle %+.1f°   Rear roll %+.2f°\n"
                              "Suspension travel FL %.1f  FR %.1f  RL %.1f  RR %.1f mm\n"
                              "Camber FL %+.2f  FR %+.2f  RL %+.2f  RR %+.2f°",
                              ch.gLat, ch.gLong, ch.gVert, ch.localVelocity[0], ch.localVelocity[1], ch.localVelocity[2],
                              ch.angularVelocity[0], ch.angularVelocity[1], ch.angularVelocity[2],
                              ch.frontWheelsAngle * 57.2958f, ch.rearRoll * 57.2958f, ch.suspensionPos[2], ch.suspensionPos[3],
                              ch.suspensionPos[0], ch.suspensionPos[1], ch.camber[2] * 57.2958f, ch.camber[3] * 57.2958f,
                              ch.camber[0] * 57.2958f, ch.camber[1] * 57.2958f);
        dl->AddLine(ImVec2(q.x, ImGui::GetCursorScreenPos().y - 2), ImVec2(q.x + puW, ImGui::GetCursorScreenPos().y - 2), C_BORDER);
    }
    if (!dm.valid) {
        textCol(C_DIM, "No damage data yet");
    } else {
        struct Row {
            const char *name;
            int v;
            bool wear;
        } rows[] = {{"ICE", dm.ice, true},  {"MGU-H", dm.mguh, true}, {"MGU-K", dm.mguk, true}, {"ES", dm.es, true},
                    {"CE", dm.ce, true},    {"TC", dm.tc, true},     {"Gearbox", dm.gearbox, false},
                    {"Engine", dm.engine, false}};
        for (const auto &r : rows) {
            ImVec2 q = ImGui::GetCursorScreenPos();
            dl->AddText(q, C_DIM, r.name);
            const float b0 = q.x + 58, b1 = q.x + puW - 40;
            dl->AddRectFilled(ImVec2(b0, q.y + 6), ImVec2(b1, q.y + 12), alpha(C_TEXT, 0.08f), 2);
            ImU32 col = r.wear ? (r.v < 50 ? alpha(C_CYAN, 0.8f) : r.v < 75 ? C_ORANGE : C_RED) : damageColour(float(r.v));
            dl->AddRectFilled(ImVec2(b0, q.y + 6), ImVec2(b0 + (b1 - b0) * std::clamp(float(r.v) / 100.f, 0.f, 1.f), q.y + 12), col, 2);
            char b[16];
            std::snprintf(b, sizeof b, "%d%%", r.v);
            ImVec2 ts = ImGui::CalcTextSize(b);
            dl->AddText(ImVec2(q.x + puW - ts.x, q.y), C_TEXT, b);
            ImGui::Dummy(ImVec2(puW, lh + 3));
        }
        ImGui::Spacing();
        if (s.events.empty()) {
            textCol(alpha(C_DIM, 0.7f), "No faults or damage");
        } else {
            const int n = int(s.events.size());
            const float left = o.y + bodyH - ImGui::GetCursorScreenPos().y;
            const int show = std::max(1, int(left / ImGui::GetTextLineHeightWithSpacing()));
            ImGui::PushTextWrapPos(px + puW - 4);
            for (int k = n - 1; k >= std::max(0, n - show); k--) {
                const auto &e = s.events[k];
                ImU32 col = e.level == 2 ? C_RED : e.level == 1 ? C_ORANGE : C_GREEN;
                textCol(col, "L%d %s", e.lap, e.text.c_str());
            }
            ImGui::PopTextWrapPos();
        }
    }
    ImGui::EndGroup();
    ImGui::PopFont();
    endCard();
}

void recentCard(app::App &a, float w, float h)
{
    beginCard("recent", ImVec2(w, h), "LAST 30 S");
    const float ph = (ImGui::GetContentRegionAvail().y - 8) / 2;
    a.trace.read([&](const TraceData &d) {
        float t1 = d.recent.empty() ? 0 : d.recent.back().t, t0 = t1 - timing::TraceLog::RECENT_S;
        std::vector<ImVec2> sp, th, br;
        for (const auto &q : d.recent) {
            sp.push_back(ImVec2(q.t, q.speed));
            th.push_back(ImVec2(q.t, q.throttle * 100));
            br.push_back(ImVec2(q.t, q.brake * 100));
        }
        Plot p = beginPlot("spd", ph, t0, t1, 0, 350, "SPEED km/h");
        plotLine(p, sp, C_CYAN, 2.f);
        ImGui::Spacing();
        Plot q = beginPlot("ped", ph, t0, t1, 0, 100, "THR / BRK %");
        plotLine(q, th, C_GREEN, 1.8f);
        plotLine(q, br, C_RED, 1.8f);
    });
    endCard();
}

// A car on the map: a triangle pointing where the car points. `dir` is in screen space.
void carMarker(ImDrawList *dl, ImVec2 c, ImVec2 dir, float size, ImU32 fill, ImU32 outline)
{
    float l = std::sqrt(dir.x * dir.x + dir.y * dir.y);
    if (l < 1e-4f) { dir = ImVec2(0, -1); l = 1; }
    const ImVec2 f(dir.x / l, dir.y / l), r(-f.y, f.x);
    const ImVec2 tip(c.x + f.x * size * 1.3f, c.y + f.y * size * 1.3f);
    const ImVec2 bl(c.x - f.x * size * 0.8f + r.x * size * 0.85f, c.y - f.y * size * 0.8f + r.y * size * 0.85f);
    const ImVec2 br(c.x - f.x * size * 0.8f - r.x * size * 0.85f, c.y - f.y * size * 0.8f - r.y * size * 0.85f);
    dl->AddTriangleFilled(tip, bl, br, fill);
    if (outline) dl->AddTriangle(tip, bl, br, outline, 1.5f);
}

// Top view of the circuit: the bundled outline (centre line + widths) aligned to the game's coordinates when there
// is one, otherwise the player's own driving line. Sector times and the game's track-limit counters beside it.
// The surface scan of a calibrated map: tarmac, kerbs, grass, gravel, hard run-off, water, as the cars' wheels found
// them. Drawn as runs of cells; only those inside the view.
constexpr ImU32 SURF_COL[tracks::S_ALL] = {IM_COL32(86, 95, 110, 255),  IM_COL32(220, 64, 64, 235),
                                                    IM_COL32(56, 122, 66, 215),  IM_COL32(178, 152, 104, 225),
                                                    IM_COL32(150, 154, 160, 210), IM_COL32(64, 124, 206, 215),
                                                    // inferred (no wheel went there): shown as what it was inferred to be
                                                    IM_COL32(86, 95, 110, 255), IM_COL32(220, 64, 64, 235),
                                                    IM_COL32(56, 122, 66, 215), IM_COL32(178, 152, 104, 225),
                                                    IM_COL32(64, 124, 206, 215)};
template <typename Map> void drawSurface(ImDrawList *dl, const tracks::Geometry &g, Map map, float, ImVec2 c0, ImVec2 c1)
{
    const auto fitted = [](float w) { return w > 0; };
    const bool kerbShapes = std::any_of(g.kl.begin(), g.kl.end(), fitted) || std::any_of(g.kr.begin(), g.kr.end(), fitted);
    for (const auto &r : g.runs) {
        const ImVec2 a = map(float(r.x0) * g.cell, float(r.z) * g.cell), b = map(float(r.x1 + 1) * g.cell, float(r.z + 1) * g.cell);
        ImVec2 p0(std::min(a.x, b.x), std::min(a.y, b.y)), p1(std::max(a.x, b.x), std::max(a.y, b.y));
        if (p1.x < c0.x || p0.x > c1.x || p1.y < c0.y || p0.y > c1.y) continue;
        if (p1.x - p0.x < 1.f) p1.x = p0.x + 1.f; // at least a pixel when zoomed out
        if (p1.y - p0.y < 1.f) p1.y = p0.y + 1.f;
        // with fitted kerbs, the kerb cells are drawn as tarmac and the kerbs as shapes over them
        const bool kerb = r.cls == tracks::S_KERB || r.cls == tracks::S_INF_KERB;
        dl->AddRectFilled(p0, p1, SURF_COL[kerbShapes && kerb ? tracks::S_TARMAC : r.cls]);
    }
}

// The outline: asphalt between the two edges, and the edges (maps without a surface scan).
template <typename Map> void drawOutline(ImDrawList *dl, const tracks::Geometry &geo, Map map)
{
    const size_t n = geo.cx.size();
    for (size_t i = 0; i < n; i++) {
        const size_t j = (i + 1) % n;
        dl->AddQuadFilled(map(geo.lx[i], geo.lz[i]), map(geo.lx[j], geo.lz[j]), map(geo.rx[j], geo.rz[j]),
                          map(geo.rx[i], geo.rz[i]), IM_COL32(44, 50, 60, 255));
    }
    std::vector<ImVec2> le, re;
    for (size_t i = 0; i < n; i++) {
        le.push_back(map(geo.lx[i], geo.lz[i]));
        re.push_back(map(geo.rx[i], geo.rz[i]));
    }
    dl->AddPolyline(le.data(), int(n), IM_COL32(170, 178, 190, 200), ImDrawFlags_Closed, 1.3f);
    dl->AddPolyline(re.data(), int(n), IM_COL32(170, 178, 190, 200), ImDrawFlags_Closed, 1.3f);
}

// A Catmull-Rom curve through p[1] .. p[2] (p[0] and p[3] are the neighbours), t in 0..1.
ImVec2 catmull(const ImVec2 p[4], float t)
{
    const float t2 = t * t, t3 = t2 * t;
    auto f = [&](float a, float b, float c, float d) {
        return 0.5f * (2 * b + (-a + c) * t + (2 * a - 5 * b + 4 * c - d) * t2 + (-a + 3 * b - 3 * c + d) * t3);
    };
    return ImVec2(f(p[0].x, p[1].x, p[2].x, p[3].x), f(p[0].y, p[1].y, p[2].y, p[3].y));
}

// The fitted map as regions (Geometry::bands): each band a strip between two offsets from the centre line, drawn
// as quads, 4 per 5 m segment along a curve through the centre line.
template <typename Map> void drawBands(ImDrawList *dl, const tracks::Geometry &geo, Map map)
{
    const size_t n = geo.cx.size();
    if (n < 4) return;
    // the centre and the unit normal towards each side at a parameter t of segment i
    auto frame = [&](size_t i, float t, int side, ImVec2 &c, ImVec2 &nrm) {
        const size_t h = (i + n - 1) % n, j = (i + 1) % n, k = (i + 2) % n;
        const ImVec2 p[4] = {ImVec2(geo.cx[h], geo.cz[h]), ImVec2(geo.cx[i], geo.cz[i]), ImVec2(geo.cx[j], geo.cz[j]),
                             ImVec2(geo.cx[k], geo.cz[k])};
        c = catmull(p, t);
        const auto &ex = side ? geo.rx : geo.lx, &ez = side ? geo.rz : geo.lz;
        auto unit = [&](size_t q) {
            const float dx = ex[q] - geo.cx[q], dz = ez[q] - geo.cz[q], l = std::max(1e-3f, std::hypot(dx, dz));
            return ImVec2(dx / l, dz / l);
        };
        const ImVec2 a = unit(i), b = unit(j);
        nrm = ImVec2(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t);
        const float l = std::max(1e-3f, std::hypot(nrm.x, nrm.y));
        nrm = ImVec2(nrm.x / l, nrm.y / l);
    };
    for (const auto &b : geo.bands) {
        if (b.i >= n) continue;
        ImVec2 pin, pout;
        for (int s = 0; s <= 4; s++) {
            const float t = float(s) / 4.f;
            ImVec2 c, nrm;
            frame(b.i, t, b.side, c, nrm);
            const float ui = b.in0 + (b.in1 - b.in0) * t, uo = b.out0 + (b.out1 - b.out0) * t;
            const ImVec2 qi = map(c.x + nrm.x * ui, c.y + nrm.y * ui), qo = map(c.x + nrm.x * uo, c.y + nrm.y * uo);
            if (s) dl->AddQuadFilled(pin, pout, qo, qi, SURF_COL[b.cls]);
            pin = qi;
            pout = qo;
        }
    }
}

// The fitted kerbs of a scanned map: bands between their inner and outer side, and (`switches`, the map viewer) the
// switch points they were fitted to as small dots.
template <typename Map> void drawKerbs(ImDrawList *dl, const tracks::Geometry &geo, Map map, bool switches = false)
{
    if (!geo.bands.empty()) { // the regions hold the kerbs
        drawBands(dl, geo, map);
        if (switches)
            for (const auto &s : geo.switches) dl->AddCircleFilled(map(s[0], s[1]), 1.6f, IM_COL32(255, 220, 90, 200), 6);
        return;
    }
    const size_t n = geo.cx.size();
    if (geo.kl.size() != n || geo.kr.size() != n || geo.klo.size() != n || geo.kro.size() != n) return;
    for (int side = 0; side < 2; side++) {
        const auto &ex = side ? geo.rx : geo.lx, &ez = side ? geo.rz : geo.lz;
        const auto &kw = side ? geo.kr : geo.kl, &ko = side ? geo.kro : geo.klo;
        // a point `w` inside the edge at point i, in world coordinates
        auto at = [&](size_t i, float w) {
            const float dx = geo.cx[i] - ex[i], dz = geo.cz[i] - ez[i], l = std::max(1e-3f, std::hypot(dx, dz));
            return ImVec2(ex[i] + dx / l * w, ez[i] + dz / l * w);
        };
        for (size_t i = 0; i < n; i++) {
            const size_t j = (i + 1) % n;
            if (kw[i] <= 0 || kw[j] <= 0) continue;
            if (std::hypot(geo.cx[j] - geo.cx[i], geo.cz[j] - geo.cz[i]) > 50.f) continue;
            // the neighbours for the curve (the segment's own ends where the kerb stops)
            const size_t h = kw[(i + n - 1) % n] > 0 ? (i + n - 1) % n : i, k = kw[(j + 1) % n] > 0 ? (j + 1) % n : j;
            const ImVec2 in[4] = {at(h, kw[h]), at(i, kw[i]), at(j, kw[j]), at(k, kw[k])};
            const ImVec2 out[4] = {at(h, ko[h]), at(i, ko[i]), at(j, ko[j]), at(k, ko[k])};
            ImVec2 pi = map(in[1].x, in[1].y), po = map(out[1].x, out[1].y);
            for (int s = 1; s <= 4; s++) {
                const ImVec2 qi = catmull(in, float(s) / 4.f), qo = catmull(out, float(s) / 4.f);
                const ImVec2 ni = map(qi.x, qi.y), no = map(qo.x, qo.y);
                dl->AddQuadFilled(po, no, ni, pi, SURF_COL[tracks::S_KERB]);
                pi = ni;
                po = no;
            }
        }
    }
    if (switches)
        for (const auto &s : geo.switches) dl->AddCircleFilled(map(s[0], s[1]), 1.6f, IM_COL32(255, 220, 90, 200), 6);
}

// The map's DRS zones as a light, see-through green on the track surface, edge to edge.
template <typename Map> void drawZones(ImDrawList *dl, const tracks::Geometry &geo, Map map)
{
    const size_t n = geo.cx.size();
    if (geo.zones.empty() || geo.dist.size() != n) return;
    for (size_t i = 0; i < n; i++) {
        const size_t j = (i + 1) % n;
        if (geo.dist[j] < geo.dist[i]) continue; // across the line
        const float d = (geo.dist[i] + geo.dist[j]) / 2;
        bool in = false;
        for (const auto &z : geo.zones) in |= z.d0 <= z.d1 ? d >= z.d0 && d <= z.d1 : d >= z.d0 || d <= z.d1;
        if (in)
            dl->AddQuadFilled(map(geo.lx[i], geo.lz[i]), map(geo.lx[j], geo.lz[j]), map(geo.rx[j], geo.rz[j]),
                              map(geo.rx[i], geo.rz[i]), IM_COL32(120, 235, 160, 60));
    }
}

// The running lap on the track edges: both edges coloured by the ERS deploy mode where the lap has been driven
// (the plain edge colour ahead), and a green strip outside the track where the DRS flap was open. `gap` is how far
// outside the edges the strip goes, in world units.
template <typename Map>
void drawLapMarks(ImDrawList *dl, const tracks::Geometry &geo, Map map, const timing::LapTrace &lap, float gap)
{
    const size_t n = geo.cx.size();
    if (lap.s.size() < 2 || geo.dist.size() != n) return;
    const float d0 = lap.s.front().dist, d1 = lap.s.back().dist;
    const ImU32 ersCol[4] = {alpha(C_DIM, 0.9f), C_CYAN, C_ORANGE, C_PURPLE};
    for (size_t i = 0; i < n; i++) {
        const size_t j = (i + 1) % n;
        const float d = geo.dist[i];
        if (geo.dist[j] < d || d < d0 || geo.dist[j] > d1) continue; // across the line, or not driven yet
        const timing::Sample q = lap.at((d + geo.dist[j]) / 2);
        const ImU32 col = ersCol[q.ers < 4 ? q.ers : 0];
        dl->AddLine(map(geo.lx[i], geo.lz[i]), map(geo.lx[j], geo.lz[j]), col, 2.5f);
        dl->AddLine(map(geo.rx[i], geo.rz[i]), map(geo.rx[j], geo.rz[j]), col, 2.5f);
        if (!q.drs) continue;
        for (int side = 0; side < 2; side++) {
            const auto &ex = side ? geo.rx : geo.lx, &ez = side ? geo.rz : geo.lz;
            auto out = [&](size_t m) { // a point `gap` outside the edge at point m
                const float dx = ex[m] - geo.cx[m], dz = ez[m] - geo.cz[m], l = std::max(1e-3f, std::hypot(dx, dz));
                return map(ex[m] + dx / l * gap, ez[m] + dz / l * gap);
            };
            dl->AddLine(out(i), out(j), C_GREEN, 3.5f);
        }
    }
}

// The suggested track edges of a scanned map: thin lines along both fitted edges.
template <typename Map> void drawEdges(ImDrawList *dl, const tracks::Geometry &geo, Map map)
{
    const size_t n = geo.cx.size();
    for (int side = 0; side < 2; side++) {
        const auto &ex = side ? geo.rx : geo.lx, &ez = side ? geo.rz : geo.lz;
        std::vector<ImVec2> line;
        line.reserve(n * 4);
        for (size_t i = 0; i < n; i++) {
            const ImVec2 p[4] = {ImVec2(ex[(i + n - 1) % n], ez[(i + n - 1) % n]), ImVec2(ex[i], ez[i]),
                                 ImVec2(ex[(i + 1) % n], ez[(i + 1) % n]), ImVec2(ex[(i + 2) % n], ez[(i + 2) % n])};
            for (int s = 0; s < 4; s++) {
                const ImVec2 q = catmull(p, float(s) / 4.f);
                line.push_back(map(q.x, q.y));
            }
        }
        dl->AddPolyline(line.data(), int(line.size()), IM_COL32(255, 255, 255, 150), ImDrawFlags_Closed, 1.2f);
    }
}

// One line under the map: what the colours mean (only for maps with a scan).
void surfaceLegend(ImDrawList *dl, const tracks::Geometry &g, ImVec2 at, bool switches = false)
{
    if (g.runs.empty() && g.bands.empty()) return;
    struct Item { ImU32 col; const char *name; } items[] = {
        {SURF_COL[0], "Tarmac"}, {IM_COL32(255, 255, 255, 150), "Suggested edge"}, {IM_COL32(255, 220, 90, 200), "Kerb switch point"}, {SURF_COL[1], "Kerb"},
        {SURF_COL[2], "Grass"}, {SURF_COL[3], "Gravel / Sand"}, {SURF_COL[4], "Concrete etc."}};
    float x = at.x;
    for (const auto &it : items) {
        if (!switches && std::string(it.name) == "Kerb switch point") continue;
        dl->AddRectFilled(ImVec2(x, at.y + 4), ImVec2(x + 12, at.y + 14), it.col, 2);
        dl->AddRect(ImVec2(x, at.y + 4), ImVec2(x + 12, at.y + 14), IM_COL32(255, 255, 255, 40), 2);
        dl->AddText(ImVec2(x + 16, at.y), C_DIM, it.name);
        x += 22 + ImGui::CalcTextSize(it.name).x + 10;
    }
}

void mapCard(app::App &a, const Snapshot &s, float w, float h)
{
    beginCard("map", ImVec2(w, h), nullptr);
    ImDrawList *dl = ImGui::GetWindowDrawList();
    textCol(C_DIM, "TRACK");
    ImGui::SameLine();
    textCol(alpha(C_DIM, 0.75f), "%s", a.trackStatus.c_str());
    // full screen: the sectors as a row over the map, which takes the whole width; else a column beside it
    const float sideW = 230, stripH = g_mapFull ? 66 : 0;
    const ImVec2 o0 = ImGui::GetCursorScreenPos(), o(o0.x, o0.y + stripH);
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float mapW = g_mapFull ? avail.x : avail.x - sideW - 12, mapH = avail.y - stripH;
    const Car *me = s.playerIdx < f1tel::MAX_CARS ? &s.cars[s.playerIdx] : nullptr;

    // sector state of the running lap, for colouring
    // (after the line the finished lap's three stay a few seconds, then all reset together: timing::lapSectors)
    ImU32 secCol[3] = {C_DIM, C_DIM, C_DIM};
    std::array<timing::SectorCell, 3> cells{};
    if (me) {
        cells = timing::lapSectors(s, s.playerIdx);
        for (int k = 0; k < 3; k++) {
            const auto st = cells[size_t(k)].state;
            secCol[k] = st == timing::SectorCell::Done || st == timing::SectorCell::Held ? markColour(cells[size_t(k)].mark)
                      : st == timing::SectorCell::Running                              ? C_TEXT
                                                                                        : alpha(C_DIM, 0.6f);
        }
    }

    const auto &geo = a.trackGeo;
    float liveDelta = 0;
    bool hasDelta = false;
    a.trace.read([&](const TraceData &d) {
        const float s1 = s.session.sector2Start > 0 ? s.session.sector2Start : d.sectorDist[0];
        const float s2 = s.session.sector3Start > 0 ? s.session.sector3Start : d.sectorDist[1];
        auto sectorOf = [&](float dist) { return s2 > 0 && dist >= s2 ? 2 : s1 > 0 && dist >= s1 ? 1 : 0; };

        // driven outline as a fallback
        const LapTrace *outline = d.bestLap >= 0 ? &d.laps[d.bestLap] : nullptr;
        for (auto it = d.laps.rbegin(); !outline && it != d.laps.rend(); ++it)
            if (it->complete) outline = &*it;
        const bool partial = !outline;
        if (!outline && !d.laps.empty()) outline = &d.laps.back();

        // bounds of whatever is drawn
        float x0 = 1e9f, x1 = -1e9f, z0 = 1e9f, z1 = -1e9f;
        auto grow = [&](float x, float z) {
            x0 = std::min(x0, x); x1 = std::max(x1, x);
            z0 = std::min(z0, z); z1 = std::max(z1, z);
        };
        if (geo.ok) {
            for (size_t i = 0; i < geo.lx.size(); i++) { grow(geo.lx[i], geo.lz[i]); grow(geo.rx[i], geo.rz[i]); }
        } else if (outline && outline->s.size() >= 10 && d.hasPositions) {
            for (const auto &q : outline->s) grow(q.x, q.z);
        } else {
            dl->AddText(ImVec2(o.x + 10, o.y + mapH / 2 - 10), C_DIM,
                        d.hasPositions ? "The track appears after a stretch of driving" : "Waiting for Motion packets (car positions)...");
            return;
        }
        const float pad = 26;
        const float k0 = std::min((mapW - 2 * pad) / std::max(1.f, x1 - x0), (mapH - 2 * pad) / std::max(1.f, z1 - z0));
        // zoomed in: the player's car stays in the middle of the map
        const bool zoomed = g_mapZoom > 1.01f && me && me->hasPosition;
        const float k = zoomed || g_mapZoom < 0.99f ? k0 * g_mapZoom : k0;
        const float midX = o.x + mapW / 2, midY = o.y + mapH / 2;
        g_mapScale = k;
        const float cxw = (zoomed ? me->worldX : (x0 + x1) / 2) + g_mapPanX;
        const float czw = (zoomed ? me->worldZ : (z0 + z1) / 2) + g_mapPanZ;
        // screen down = world +z, as Track N Race and the game draw it (up = +z mirrored the track: right turns looked left)
        auto map = [&](float x, float z) { return ImVec2(midX + (x - cxw) * k, midY + (z - czw) * k); };
        dl->PushClipRect(o, ImVec2(o.x + mapW, o.y + mapH), true);

        if (geo.ok) {
            const size_t n = geo.cx.size();
            // a scanned map shows the scan only; otherwise asphalt between the two edges, the edges, and the centre
            // line coloured by sector
            const bool scanned = !geo.runs.empty() || !geo.bands.empty();
            if (!scanned) drawOutline(dl, geo, map);
            drawSurface(dl, geo, map, k, o, ImVec2(o.x + mapW, o.y + mapH));
            if (scanned) { drawKerbs(dl, geo, map); drawEdges(dl, geo, map); }
            drawZones(dl, geo, map);
            if (!d.laps.empty()) drawLapMarks(dl, geo, map, d.laps.back(), 6.f / k);
            surfaceLegend(dl, geo, ImVec2(o.x + 8, o.y + mapH - 2 * ImGui::GetTextLineHeight() - 8)); // above the hover hint
            for (size_t i = 0; i < n && !scanned; i++) {
                const size_t j = (i + 1) % n;
                if (std::fabs(geo.dist[j] - geo.dist[i]) > 50) continue; // across the start line
                if (g_mapZoom > 2.5f) break; // zoomed in: the driving lines take over
                dl->AddLine(map(geo.cx[i], geo.cz[i]), map(geo.cx[j], geo.cz[j]), alpha(secCol[sectorOf(geo.dist[i])], 0.8f), 2.f);
            }
            // a line across the track at a lap distance
            auto across = [&](float dist, const char *label, ImU32 col, float th) {
                size_t best = 0;
                float bd = 1e9f;
                for (size_t i = 0; i < n; i++) {
                    float dd = std::fabs(geo.dist[i] - dist);
                    if (dd < bd) { bd = dd; best = i; }
                }
                ImVec2 l = map(geo.lx[best], geo.lz[best]), r = map(geo.rx[best], geo.rz[best]);
                dl->AddLine(l, r, col, th);
                dl->AddText(ImVec2(l.x + (l.x - r.x) * 0.6f - 6, l.y + (l.y - r.y) * 0.6f - 8), col, label);
            };
            across(0, "S/F", IM_COL32_WHITE, 4.f);
            if (s1 > 0) across(s1, "S2", C_TEXT, 2.f);
            if (s2 > 0) across(s2, "S3", C_TEXT, 2.f);
        } else {
            std::vector<ImVec2> pts;
            const size_t step = std::max<size_t>(1, outline->s.size() / 900);
            for (size_t i = 0; i < outline->s.size(); i += step) pts.push_back(map(outline->s[i].x, outline->s[i].z));
            dl->AddPolyline(pts.data(), int(pts.size()), IM_COL32(8, 10, 13, 255), partial ? 0 : ImDrawFlags_Closed, 11.f);
            dl->AddPolyline(pts.data(), int(pts.size()), IM_COL32(52, 60, 72, 255), partial ? 0 : ImDrawFlags_Closed, 8.f);
            for (size_t i = step, j = 1; i < outline->s.size(); i += step, j++)
                dl->AddLine(pts[j - 1], pts[j], secCol[sectorOf(outline->s[i].dist)], 3.f);
        }

        // zoomed in: the driving lines of the fastest lap (purple) and the running lap (white)
        if (zoomed) {
            auto line = [&](const LapTrace &l, ImU32 col, float th) {
                std::vector<ImVec2> pts;
                for (const auto &q : l.s) pts.push_back(map(q.x, q.z));
                if (pts.size() > 1) dl->AddPolyline(pts.data(), int(pts.size()), col, 0, th);
            };
            if (d.bestLap >= 0 && &d.laps[d.bestLap] != &d.laps.back()) line(d.laps[d.bestLap], alpha(C_PURPLE, 0.9f), 2.f);
            if (!d.laps.empty()) line(d.laps.back(), alpha(IM_COL32_WHITE, 0.85f), 2.f);
        }

        // track-limit marks of the last three laps: the game's warnings first, wheels off the track second
        const int lapNow = d.laps.empty() ? 0 : d.laps.back().lapNum;
        const TrackMark *hover = nullptr;
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        for (const auto &m : d.marks) {
            if (m.lap < lapNow - 2) continue;
            const float fade = m.lap == lapNow ? 1.f : m.lap == lapNow - 1 ? 0.6f : 0.35f;
            ImVec2 c = map(m.x, m.z);
            switch (m.kind) {
            case MarkKind::OffTrack:
                dl->AddCircleFilled(c, 4.5f, alpha(C_ORANGE, fade));
                break;
            case MarkKind::CornerCut:
            case MarkKind::Warning: {
                const ImU32 col = alpha(m.kind == MarkKind::CornerCut ? C_RED : C_YELLOW, fade);
                dl->AddTriangleFilled(ImVec2(c.x, c.y - 8), ImVec2(c.x - 7, c.y + 5), ImVec2(c.x + 7, c.y + 5), col);
                dl->AddText(ImVec2(c.x - 2, c.y - 7), IM_COL32(0, 0, 0, 255), "!");
                break;
            }
            case MarkKind::LapInvalid:
                dl->AddLine(ImVec2(c.x - 5, c.y - 5), ImVec2(c.x + 5, c.y + 5), alpha(C_RED, fade), 2.5f);
                dl->AddLine(ImVec2(c.x - 5, c.y + 5), ImVec2(c.x + 5, c.y - 5), alpha(C_RED, fade), 2.5f);
                break;
            }
            if (std::fabs(mouse.x - c.x) < 8 && std::fabs(mouse.y - c.y) < 8) hover = &m;
        }

        // cars: everyone dim, the player on top
        struct Label {
            ImVec2 pp;
            std::string code;
            ImU32 col;
            bool lapped, mine;
            ImVec2 dir; // heading on screen
        };
        std::vector<Label> labels;
        std::vector<ImVec2> carPts;
        for (int pass = 0; pass < 2; pass++)
            for (int i : s.order()) {
                const auto &c = s.cars[i];
                const bool mine = i == s.playerIdx;
                if (!c.hasPosition || mine != (pass == 1)) continue;
                const ImVec2 pp = map(c.worldX, c.worldZ), ahead = map(c.worldX + c.fwdX, c.worldZ + c.fwdZ);
                const ImVec2 dir(ahead.x - pp.x, ahead.y - pp.y);
                if (mine) {
                    dl->AddCircleFilled(pp, 11, alpha(IM_COL32_WHITE, 0.18f));
                    carMarker(dl, pp, dir, 8, IM_COL32_WHITE, C_ACCENT);
                } else {
                    carMarker(dl, pp, dir, 5, alpha(teamColour(c.teamId), 0.8f), 0);
                }
                carPts.push_back(pp);
                // on track (not anywhere in the pit lane or the garage): a leader line with the driver's code
                if (c.pitStatus != 0 || (c.resultStatus && c.resultStatus != 2)) continue;
                // race or sprint: a car you have lapped, or will lap within 300 m, gets a paler colour and a blue flag
                // (it is shown blue flags and has to let you by once you catch it)
                const int st = s.session.sessionType;
                const float L = s.session.trackLength;
                bool lapped = false;
                if (!mine && me && L > 0 && st >= 15 && st <= 17)
                    lapped = (me->lapNum + me->lapDistance / L) - (c.lapNum + c.lapDistance / L) >= 1.f - 300.f / L;
                ImU32 lc = alpha(teamColour(c.teamId), mine ? 1.f : 0.9f); // human or AI: the team colour
                if (lapped) {
                    ImVec4 v = ImGui::ColorConvertU32ToFloat4(lc);
                    float h, sat, val;
                    ImGui::ColorConvertRGBtoHSV(v.x, v.y, v.z, h, sat, val);
                    ImGui::ColorConvertHSVtoRGB(h, sat * 0.45f, val * 0.8f, v.x, v.y, v.z);
                    lc = ImGui::ColorConvertFloat4ToU32(v);
                }
                labels.push_back({pp, displayName(c), lc, lapped, mine, dir});
            }
        // the labels: a first line square to the track at the car, pointing out of the track's loop, then a level
        // line with the code above it (out upwards) or under it (out downwards)
        std::vector<ImVec2> loop;
        if (geo.ok)
            for (size_t i = 0; i < geo.cx.size(); i += 2) loop.push_back(map(geo.cx[i], geo.cz[i]));
        auto inside = [&](ImVec2 q) { // even-odd rule
            bool in = false;
            for (size_t i = 0, j = loop.size() - 1; i < loop.size(); j = i++)
                if ((loop[i].y > q.y) != (loop[j].y > q.y) &&
                    q.x < (loop[j].x - loop[i].x) * (q.y - loop[i].y) / (loop[j].y - loop[i].y) + loop[i].x)
                    in = !in;
            return in;
        };
        for (const auto &l : labels) {
            const ImVec2 ts = ImGui::CalcTextSize(l.code.c_str());
            const float flagW = l.lapped ? ts.y * 0.9f + 4 : 0, segW = ts.x + flagW + 10;
            // the track's direction at the car: the nearest map point's neighbours, else the car's heading
            ImVec2 tg = l.dir;
            if (loop.size() >= 3) {
                size_t best = 0;
                float bd = 1e30f;
                for (size_t i = 0; i < loop.size(); i++) {
                    const float d = (loop[i].x - l.pp.x) * (loop[i].x - l.pp.x) + (loop[i].y - l.pp.y) * (loop[i].y - l.pp.y);
                    if (d < bd) bd = d, best = i;
                }
                const ImVec2 &pa = loop[(best + loop.size() - 1) % loop.size()], &pb = loop[(best + 1) % loop.size()];
                tg = ImVec2(pb.x - pa.x, pb.y - pa.y);
            }
            const float tl = std::max(1e-3f, std::hypot(tg.x, tg.y));
            ImVec2 n(-tg.y / tl, tg.x / tl);
            // outwards: out of the loop; without a map, away from the map's middle
            const ImVec2 probe(l.pp.x + n.x * 12, l.pp.y + n.y * 12);
            const bool flip = loop.size() >= 3 ? inside(probe)
                                               : (n.x * (l.pp.x - midX) + n.y * (l.pp.y - midY)) < 0;
            if (flip) n = ImVec2(-n.x, -n.y);
            const float len = 30;
            ImVec2 knee(l.pp.x + n.x * len, l.pp.y + n.y * len);
            const float hx = std::fabs(n.x) > 0.2f ? (n.x > 0 ? 1.f : -1.f) : (l.pp.x >= midX ? 1.f : -1.f);
            const bool above = n.y <= 0;
            // keep it on the map: the level line turns back at the sides, the whole label moves in at the top/bottom
            float sx = hx;
            if (knee.x + sx * segW > o.x + mapW - 2 || knee.x + sx * segW < o.x + 2) sx = -sx;
            const float top = above ? knee.y - ts.y - 3 : knee.y, bottom = above ? knee.y : knee.y + ts.y + 3;
            if (top < o.y + 2) knee.y += o.y + 2 - top;
            if (bottom > o.y + mapH - 2) knee.y -= bottom - (o.y + mapH - 2);
            knee.x = std::clamp(knee.x, o.x + 2, o.x + mapW - 2);
            const ImVec2 end(knee.x + sx * segW, knee.y);
            const float tx = std::min(knee.x, end.x) + 5;
            const ImVec2 tp(tx, above ? knee.y - ts.y - 2 : knee.y + 2);
            const ImVec2 leader[3] = {l.pp, knee, end};
            dl->AddPolyline(leader, 3, alpha(l.col, 0.85f), 0, 2.f);
            // the code above the far end of the line, a lapped car's with a blue flag beside it
            dl->AddText(tp, l.col, l.code.c_str());
            if (l.lapped) {
                const float fx = tp.x + ts.x + 4, fy = tp.y + 2, fh = ts.y - 4;
                dl->AddLine(ImVec2(fx, fy), ImVec2(fx, fy + fh), alpha(C_TEXT, 0.8f), 1.5f);
                dl->AddRectFilled(ImVec2(fx + 1, fy), ImVec2(fx + fh * 0.8f, fy + fh * 0.55f), IM_COL32(40, 120, 255, 255));
            }
        }
        if (hover) {
            static const char *kinds[] = {"Off track (two or more wheels on grass / gravel etc.)", "Corner-cutting warning (game)", "Warning (game)", "Lap invalidated (game)"};
            ImGui::SetTooltip("Lap %d  %.0f m\n%s", hover->lap, hover->dist, kinds[int(hover->kind)]);
        }

        dl->PopClipRect();

        // live delta to the fastest lap at the player's distance
        if (!d.laps.empty() && d.bestLap >= 0 && &d.laps.back() != &d.laps[d.bestLap]) {
            const auto &cur = d.laps.back();
            if (cur.s.size() > 2) {
                const float rt = d.laps[d.bestLap].timeAt(cur.s.back().dist);
                if (rt >= 0) {
                    liveDelta = (cur.s.back().t - cur.s.front().t) - rt;
                    hasDelta = true;
                }
            }
        }
    });
    ImGui::SetCursorScreenPos(o);
    ImGui::SetNextItemAllowOverlap(); // the zoom buttons on top of it must get the clicks
    // wheel zooms (around the car), left or right drag pans, double click shows the whole circuit again
    ImGui::InvisibleButton("mapzoom", ImVec2(mapW, mapH), ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    const bool mapHover = ImGui::IsItemHovered();
    if (mapHover && ImGui::GetIO().MouseWheel != 0) {
        g_mapZoom = std::clamp(g_mapZoom * std::pow(1.25f, ImGui::GetIO().MouseWheel), MAP_ZOOM_MIN, 24.f);
        if (g_mapZoom <= 1.01f) g_mapPanX = g_mapPanZ = 0;
    }
    if (ImGui::IsItemActive() && g_mapScale > 0) {
        const ImVec2 md = ImGui::GetIO().MouseDelta;
        g_mapPanX -= md.x / g_mapScale;
        g_mapPanZ -= md.y / g_mapScale;
    }
    if (mapHover && ImGui::IsMouseDoubleClicked(0)) g_mapZoom = 1, g_mapPanX = g_mapPanZ = 0;
    if (mapHover && !ImGui::IsItemActive()) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    if (ImGui::IsItemActive()) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
    {
        const ImVec2 back = ImGui::GetCursorScreenPos();
        const bool panned = std::fabs(g_mapPanX) + std::fabs(g_mapPanZ) > 0.5f;
        // the buttons' row measured from their labels (any font), right-aligned in the map
        const char *fullLabel = g_mapFull ? "EXIT FULL" : "FULL";
        auto bw = [](const char *t) { return ImGui::CalcTextSize(t).x + 2 * ImGui::GetStyle().FramePadding.x; };
        const float rowW = bw(fullLabel) + (panned ? bw("BACK TO CAR") + 4 : 0) + bw(" - ") + bw(" + ") + bw("FIT") + 3 * 4;
        // full screen: on the title line at the card's top right, clear of the floating cards
        const float by = g_mapFull ? o0.y - ImGui::GetFrameHeightWithSpacing() : o.y + 2;
        const float bx = o.x + mapW - rowW - 6;
        ImGui::SetCursorScreenPos(ImVec2(bx, by));
        if (ImGui::SmallButton(g_mapFull ? "EXIT FULL" : "FULL")) g_mapFull = !g_mapFull;
        ImGui::SameLine(0, 4);
        if (panned) {
            if (ImGui::SmallButton("BACK TO CAR")) g_mapPanX = g_mapPanZ = 0;
            ImGui::SameLine(0, 4);
        }
        if (ImGui::SmallButton(" - ")) {
            g_mapZoom = std::max(MAP_ZOOM_MIN, g_mapZoom / 1.5f);
            if (g_mapZoom <= 1.01f) g_mapPanX = g_mapPanZ = 0;
        }
        ImGui::SameLine(0, 4);
        if (ImGui::SmallButton(" + ")) g_mapZoom = std::min(24.f, g_mapZoom * 1.5f);
        ImGui::SameLine(0, 4);
        ImGui::BeginDisabled(std::fabs(g_mapZoom - 1) < 0.01f);
        if (ImGui::SmallButton("FIT")) g_mapZoom = 1, g_mapPanX = g_mapPanZ = 0;
        ImGui::EndDisabled();
        if (std::fabs(g_mapZoom - 1) >= 0.01f) {
            char zb[16];
            std::snprintf(zb, sizeof zb, "%.1fx", g_mapZoom);
            dl->AddText(ImVec2(bx - ImGui::CalcTextSize(zb).x - 8, by + 2), C_DIM, zb);
        }
        if (mapHover) dl->AddText(ImVec2(o.x + 4, o.y + mapH - ImGui::GetTextLineHeight() - 2), alpha(C_DIM, 0.7f),
                                  g_mapFull ? "Wheel: zoom · L/R drag: pan · Double-click: fit · Esc: exit full screen" : "Wheel: zoom · L/R drag: pan · Double-click: fit");
        ImGui::SetCursorScreenPos(back);
    }

    // full screen: one row of sectors, delta and the game's track-limit counters over the map
    if (g_mapFull && me) {
        const float rowH = stripH - 8, bw = 200;
        for (int k = 0; k < 3; k++) {
            const uint32_t ms = cells[size_t(k)].ms;
            const bool done = cells[size_t(k)].state == timing::SectorCell::Done || cells[size_t(k)].state == timing::SectorCell::Held;
            const bool live = cells[size_t(k)].state == timing::SectorCell::Running;
            const float x = o0.x + k * (bw + 8), y = o0.y;
            dl->AddRectFilled(ImVec2(x, y), ImVec2(x + bw, y + rowH), C_PANEL2, 5);
            dl->AddRectFilled(ImVec2(x, y), ImVec2(x + 4, y + rowH), secCol[k], 2);
            char b[16];
            std::snprintf(b, sizeof b, "S%d", k + 1);
            dl->AddText(ImVec2(x + 12, y + 5), C_DIM, b);
            std::string best = "BEST " + formatSector(me->bestSectorMs[k]);
            ImVec2 bs = ImGui::CalcTextSize(best.c_str());
            dl->AddText(ImVec2(x + bw - bs.x - 10, y + 5), alpha(C_DIM, 0.85f), best.c_str());
            if (done)
                dl->AddText(F->numMid, fsz(F->numMid) * 0.7f, ImVec2(x + 12, y + rowH - fsz(F->numMid) * 0.7f - 4),
                            secCol[k], formatSector(ms).c_str());
            else
                dl->AddText(ImVec2(x + 12, y + rowH - 24), live ? C_TEXT : C_DIM,
                            live ? runningLabel(*me) : "--.---");
        }
        const auto &pl = s.player;
        ImGui::SetCursorScreenPos(ImVec2(o0.x + 3 * (bw + 8) + 10, o0.y + 2));
        ImGui::BeginGroup();
        textCol(C_DIM, "DELTA TO BEST");
        ImGui::SameLine();
        withFont(F->num, [&] {
            if (hasDelta) textCol(liveDelta > 0 ? C_RED : C_GREEN, "%+.3f", liveDelta);
            else textCol(C_DIM, "--");
        });
        ImGui::SameLine(0, 24);
        textCol(me->currentLapInvalid ? C_RED : C_GREEN, me->currentLapInvalid ? "LAP INVALID" : "LAP VALID");
        textCol(pl.cornerCuttingWarnings ? C_RED : C_TEXT, "Cut warnings %u", unsigned(pl.cornerCuttingWarnings));
        ImGui::SameLine(0, 16);
        textCol(pl.totalWarnings ? C_YELLOW : C_TEXT, "Warnings %u", unsigned(pl.totalWarnings));
        ImGui::SameLine(0, 16);
        textCol(pl.penaltiesS ? C_RED : C_TEXT, "Penalty %us", unsigned(pl.penaltiesS));
        ImGui::EndGroup();
        // the forecast in the rest of the row, up to the car card floating at the right (same sizes as its layout)
        const float wx = ImGui::GetItemRectMax().x + 28;
        const float right = o0.x + ImGui::GetWindowWidth() - 14 - std::max(240.f, ImGui::GetWindowWidth() * 0.15f) - 40;
        if (right - wx > 240) {
            dl->AddRectFilled(ImVec2(wx, o0.y), ImVec2(right, o0.y + rowH), C_PANEL2, 5);
            g_weatherSnap = &s;
            weatherStrip(dl, ImVec2(wx + 4, o0.y), ImVec2(right - wx - 8, rowH));
        }
    }

    // side column: sectors, delta, the game's track-limit counters
    ImGui::SetCursorScreenPos(ImVec2(o.x + mapW + 12, o.y));
    ImGui::BeginGroup();
    if (me && !g_mapFull) {
        ImVec2 q = ImGui::GetCursorScreenPos();
        // short card (small window or large UI scale): the rows shrink to a floor and the text under them drops out
        const bool roomy = mapH >= 3 * (34 + 8) + 150;
        const float rowH = std::clamp((mapH - (roomy ? 150 : 0)) / 3 - 8, 34.f, 60.f);
        for (int k = 0; k < 3; k++) {
            const uint32_t ms = cells[size_t(k)].ms;
            const bool done = cells[size_t(k)].state == timing::SectorCell::Done || cells[size_t(k)].state == timing::SectorCell::Held;
            const bool live = cells[size_t(k)].state == timing::SectorCell::Running;
            const float y = q.y + k * (rowH + 8);
            dl->AddRectFilled(ImVec2(q.x, y), ImVec2(q.x + sideW, y + rowH), C_PANEL2, 5);
            dl->AddRectFilled(ImVec2(q.x, y), ImVec2(q.x + 4, y + rowH), secCol[k], 2);
            char b[16];
            std::snprintf(b, sizeof b, "S%d", k + 1);
            dl->AddText(ImVec2(q.x + 12, y + 6), C_DIM, b);
            std::string best = "BEST " + formatSector(me->bestSectorMs[k]);
            ImVec2 bs = ImGui::CalcTextSize(best.c_str());
            dl->AddText(ImVec2(q.x + sideW - bs.x - 10, y + 6), alpha(C_DIM, 0.85f), best.c_str());
            if (done)
                dl->AddText(F->numMid, fsz(F->numMid) * 0.8f, ImVec2(q.x + 12, y + rowH - fsz(F->numMid) * 0.8f - 5),
                            secCol[k], formatSector(ms).c_str());
            else
                dl->AddText(ImVec2(q.x + 12, y + rowH - 25), live ? C_TEXT : C_DIM,
                            live ? runningLabel(*me) : "--.---");
        }
        ImGui::Dummy(ImVec2(sideW, 3 * (rowH + 8)));
        if (roomy) {
        textCol(C_DIM, "DELTA TO BEST");
        ImGui::SameLine();
        withFont(F->num, [&] {
            if (hasDelta) textCol(liveDelta > 0 ? C_RED : C_GREEN, "%+.3f", liveDelta);
            else textCol(C_DIM, "--");
        });
        ImGui::Spacing();
        textCol(C_DIM, "TRACK LIMITS (GAME)");
        const auto &pl = s.player;
        textCol(pl.cornerCuttingWarnings ? C_RED : C_TEXT, "Cut warnings %u", unsigned(pl.cornerCuttingWarnings));
        ImGui::SameLine();
        textCol(pl.totalWarnings ? C_YELLOW : C_TEXT, "Warnings %u", unsigned(pl.totalWarnings));
        textCol(pl.penaltiesS ? C_RED : C_TEXT, "Penalty %us", unsigned(pl.penaltiesS));
        ImGui::SameLine();
        textCol(me->currentLapInvalid ? C_RED : C_GREEN, me->currentLapInvalid ? "LAP INVALID" : "LAP VALID");
        }
    }
    ImGui::EndGroup();
    endCard();
}

void standingsCard(app::App &a, const Snapshot &s, float w, float h)
{
    // column widths: as wide as their content in the table's font (and the header in the UI font)
    ImGui::PushFont(F->num);
    const float posW = ImGui::CalcTextSize("20").x + 2, drvNum = ImGui::CalcTextSize("WWW").x + 12;
    const float timeW = ImGui::CalcTextSize("0:00.000").x + 2, gapW = ImGui::CalcTextSize("+000.000").x + 2;
    const float tyreW = ImGui::CalcTextSize("00").x + ImGui::GetTextLineHeight() + 4;
    ImGui::PopFont();
    const float drvW = std::max(drvNum, ImGui::CalcTextSize("DRIVER").x + 2);
    // full-screen map: the card only as wide as the table (no empty column over the map)
    if (g_mapFull) {
        const float cellPad = 5, need = posW + drvW + gapW + 2 * timeW + tyreW + 6 * 2 * cellPad + 2 * cellPad + 2 * 14 +
                                         ImGui::GetStyle().ScrollbarSize + 4;
        w = std::min(w, need);
    }
    beginScrollCard("standings", ImVec2(w, h), "STANDINGS");
    static const char *modes[] = {"TO ME", "INTERVAL", "TO LEADER"};
    // right-aligned, measured from the labels (any font)
    float modesW = 2 * 2;
    for (const char *m : modes) modesW += ImGui::CalcTextSize(m).x + 2 * ImGui::GetStyle().FramePadding.x;
    ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - modesW);
    const float modesY = ImGui::GetCursorPosY() - 4; // all three on the same height
    for (int m = 0; m < 3; m++) {
        if (m) ImGui::SameLine(0, 2);
        ImGui::SetCursorPosY(modesY);
        bool sel = a.cfg.gapMode == m;
        if (sel) ImGui::PushStyleColor(ImGuiCol_Button, v4(C_PANEL2));
        if (ImGui::SmallButton(modes[m])) { a.cfg.gapMode = m; a.dirty = true; }
        if (sel) ImGui::PopStyleColor();
    }
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(5, 5));
    // every column as wide as its content in the table's font (the driver code used to be a stretch column and came
    // out as "D..."); the spare width goes to an empty last column, so the numbers stay together in a wide card. No
    // saved settings: widths from an older layout must not stick. In the pit lane the position cell turns blue.
    if (ImGui::BeginTable("tbl", 7, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_PadOuterX | ImGuiTableFlags_NoSavedSettings)) {
        ImGui::TableSetupColumn("P", ImGuiTableColumnFlags_WidthFixed, posW);
        ImGui::TableSetupColumn("DRIVER", ImGuiTableColumnFlags_WidthFixed, drvW);
        ImGui::TableSetupColumn("GAP", ImGuiTableColumnFlags_WidthFixed, gapW);
        ImGui::TableSetupColumn("LAST LAP", ImGuiTableColumnFlags_WidthFixed, timeW);
        ImGui::TableSetupColumn("BEST", ImGuiTableColumnFlags_WidthFixed, timeW);
        ImGui::TableSetupColumn("TYRE", ImGuiTableColumnFlags_WidthFixed, tyreW);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch);
        ImGui::PushStyleColor(ImGuiCol_Text, v4(C_DIM));
        ImGui::TableHeadersRow();
        ImGui::PopStyleColor();
        ImGui::PushFont(F->num);
        for (int i : s.order()) {
            const auto &c = s.cars[i];
            const bool me = i == s.playerIdx;
            ImGui::TableNextRow();
            if (me) ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, alpha(IM_COL32_WHITE, 0.12f));
            ImGui::TableNextColumn();
            // in the pit lane (any session) the position goes white on blue, as on the HMD
            const bool pitCell = c.pitStatus != 0;
            if (pitCell) {
                ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, C_BLUE);
                textCol(IM_COL32_WHITE, "%d", c.position);
            } else {
                ImGui::Text("%d", c.position);
            }
            ImGui::TableNextColumn();
            {
                ImVec2 q = ImGui::GetCursorScreenPos();
                ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(q.x, q.y + 2), ImVec2(q.x + 4, q.y + ImGui::GetTextLineHeight() - 2),
                                                          teamColour(c.teamId));
                ImGui::SetCursorScreenPos(ImVec2(q.x + 10, q.y));
                textCol(me ? C_TEXT : alpha(C_TEXT, 0.9f), "%s", displayName(c).c_str());
            }
            ImGui::TableNextColumn();
            std::string g = gapText(s, i, a.cfg.gapMode);
            textCol(me ? C_TEXT : C_DIM, "%s", g.c_str());
            ImGui::TableNextColumn();
            Mark lm = c.laps.empty() ? Mark::None : markLap(s, i, c.lastLapMs, c.laps.back().lapValid);
            textCol(lm == Mark::SessionBest ? C_PURPLE : lm == Mark::Invalid ? C_RED : C_TEXT, "%s",
                    formatLap(c.lastLapMs).c_str());
            ImGui::TableNextColumn();
            textCol(c.bestLapMs && c.bestLapMs == s.sessionBestLapMs ? C_PURPLE : C_DIM, "%s", formatLap(c.bestLapMs).c_str());
            ImGui::TableNextColumn();
            {
                const char *l;
                ImU32 col;
                tyreInfo(c.visualTyre, l, col);
                // the compound as a coloured dot (no letter: it depends on the font); its name on hover
                ImVec2 q = ImGui::GetCursorScreenPos();
                const float r = ImGui::GetTextLineHeight() / 2;
                ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(q.x + r, q.y + r), r * 0.62f, col);
                if (ImGui::IsMouseHoveringRect(q, ImVec2(q.x + 2 * r, q.y + 2 * r))) ImGui::SetTooltip("%s", *l == 'S' ? "Soft" : *l == 'M' ? "Medium" : *l == 'H' ? "Hard" : *l == 'I' ? "Intermediate" : *l == 'W' ? "Wet" : "Unknown");
                ImGui::SetCursorScreenPos(ImVec2(q.x + 2 * r + 4, q.y));
                textCol(C_DIM, "%d", c.tyreAgeLaps);
            }
            ImGui::TableNextColumn(); // spare width
        }
        ImGui::PopFont();
        ImGui::EndTable();
    }
    ImGui::PopStyleVar();
    endCard();
}

// The session's weather forecast for the next 45 minutes as a strip of cells (when, symbol, chance of rain, track
// temperature), so a human in a GP can see what is coming; the same samples as the HMD weather panel. Tall: the
// cells stack time / symbol / numbers; short (the full-screen map's top row): symbol left, time and numbers right.
void weatherStrip(ImDrawList *dl, ImVec2 p0, ImVec2 sz)
{
    const Snapshot &s = *g_weatherSnap;
    const float fs = fsz(F->small);
    auto put = [&](float x, float y, ImU32 col, const char *t, float align) { // align 0 left, 0.5 centre
        const float tw = F->small->CalcTextSizeA(fs, FLT_MAX, 0, t).x;
        dl->AddText(F->small, fs, ImVec2(x - tw * align, y), col, t);
    };
    std::vector<timing::Forecast> fc;
    for (const auto &f : s.session.forecast)
        if (f.minutes <= 45) fc.push_back(f);
    if (!s.session.valid || fc.empty()) {
        put(p0.x + sz.x / 2, p0.y + sz.y / 2 - fs / 2, C_DIM, "NO FORECAST", 0.5f);
        return;
    }
    const int n = int(fc.size());
    const float cw = sz.x / float(n);
    const bool tall = sz.y >= 70;
    for (int k = 0; k < n; k++) {
        const auto &f = fc[k];
        const float x0 = p0.x + cw * float(k), cx = x0 + cw / 2;
        if (k) dl->AddLine(ImVec2(x0, p0.y + 6), ImVec2(x0, p0.y + sz.y - 6), C_BORDER);
        char when[16], num[24];
        if (f.minutes == 0) std::snprintf(when, sizeof when, "NOW");
        else std::snprintf(when, sizeof when, "+%u'", unsigned(f.minutes));
        std::snprintf(num, sizeof num, "%u%% %d°", unsigned(f.rain), int(f.trackTemp));
        const ImU32 rc = f.rain >= 50 ? C_BLUE : f.rain > 0 ? C_TEXT : C_DIM;
        if (tall) {
            const float icon = std::min({30.f, cw * 0.55f, sz.y - 2 * fs - 12});
            put(cx, p0.y + 4, f.minutes == 0 ? C_TEXT : C_DIM, when, 0.5f);
            panels::weatherSymbol(dl, ImVec2(cx, p0.y + 6 + fs + icon / 2), icon, f.weather);
            put(cx, p0.y + sz.y - fs - 4, rc, num, 0.5f);
        } else {
            const float icon = std::min(sz.y * 0.7f, cw * 0.32f);
            panels::weatherSymbol(dl, ImVec2(x0 + 8 + icon / 2, p0.y + sz.y / 2), icon, f.weather);
            const float tx = x0 + 14 + icon;
            put(tx, p0.y + sz.y / 2 - fs - 1, f.minutes == 0 ? C_TEXT : C_DIM, when, 0);
            put(tx, p0.y + sz.y / 2 + 1, rc, num, 0);
        }
    }
}

void weatherCard(const Snapshot &s, float w, float h)
{
    beginCard("weather", ImVec2(w, h));
    g_weatherSnap = &s;
    const ImVec2 p0 = ImGui::GetWindowPos(), sz = ImGui::GetWindowSize();
    weatherStrip(ImGui::GetWindowDrawList(), ImVec2(p0.x + 8, p0.y), ImVec2(sz.x - 16, sz.y));
    endCard();
}

void pageDashboard(app::App &a, const Snapshot &s)
{
    if (emptyState(a, s)) return;
    const float gap = 12;
    const float W = ImGui::GetContentRegionAvail().x, H = ImGui::GetContentRegionAvail().y;
    if (g_mapFull) { // the map over everything, the other cards smaller and floating over its edges
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) g_mapFull = false;
        const ImVec2 o = ImGui::GetCursorScreenPos();
        mapCard(a, s, W, H);
        // left: the standings under the sector row; right column, top down: the car (narrow), tyres and condition
        // (no last-30-s trace here: little use while driving)
        const float m = 14, top = 48 + 66 + 6; // under the map card's title and sector row
        const float leftW = std::max(520.f, W * 0.31f), carW = std::max(240.f, W * 0.15f);
        const float statusW = std::max(520.f, W * 0.36f), statusH = std::min(520.f, H * 0.36f);
        const float carTop = 48;
        const float carH = std::max(300.f, H - carTop - m - statusH - gap);
        const float rx = o.x + W - m;
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.94f);
        ImGui::SetCursorScreenPos(ImVec2(o.x + m, o.y + top));
        standingsCard(a, s, leftW, H - top - m);
        ImGui::SetCursorScreenPos(ImVec2(rx - carW, o.y + carTop));
        carCard(s, carW, carH);
        ImGui::SetCursorScreenPos(ImVec2(rx - statusW, o.y + H - statusH - m));
        carStatusCard(s, statusW, statusH);
        ImGui::PopStyleVar();
        return;
    }
    const Car *me = s.playerIdx < f1tel::MAX_CARS ? &s.cars[s.playerIdx] : nullptr;

    // top: six numbers and the forecast (two cards wide)
    const float cw = (W - 7 * gap) / 8;
    char pos[32] = "-", lap[32] = "-";
    if (me) {
        std::snprintf(pos, sizeof pos, "P%d", me->position);
        std::snprintf(lap, sizeof lap, "%d", me->lapNum);
    }
    char of[32];
    std::snprintf(of, sizeof of, "/ %d", int(s.order().size()));
    statCard("c1", cw, "POSITION", pos, C_TEXT, of);
    ImGui::SameLine(0, gap);
    statCard("c2", cw, "LAP", lap);
    ImGui::SameLine(0, gap);
    statCard("c3", cw, me && me->currentLapInvalid ? "CURRENT (INVALID)" : "CURRENT", me ? formatLap(me->currentLapMs) : "-",
             me && me->currentLapInvalid ? C_RED : C_TEXT);
    ImGui::SameLine(0, gap);
    {
        Mark m = me && !me->laps.empty() ? markLap(s, s.playerIdx, me->lastLapMs, me->laps.back().lapValid) : Mark::None;
        statCard("c4", cw, "LAST LAP", me ? formatLap(me->lastLapMs) : "-", m == Mark::None ? C_TEXT : markColour(m));
    }
    ImGui::SameLine(0, gap);
    statCard("c5", cw, "PERSONAL BEST", me ? formatLap(me->bestLapMs) : "-",
             me && me->bestLapMs && me->bestLapMs == s.sessionBestLapMs ? C_PURPLE : C_GREEN);
    ImGui::SameLine(0, gap);
    statCard("c6", cw, "SESSION BEST", formatLap(s.sessionBestLapMs), C_PURPLE);
    ImGui::SameLine(0, gap);
    weatherCard(s, 2 * cw + gap, 84);
    ImGui::Dummy(ImVec2(0, gap - ImGui::GetStyle().ItemSpacing.y));

    const float sp = ImGui::GetStyle().ItemSpacing.y;
    const float rest = H - 84 - gap - sp;
    const float leftW = std::max(320.f, W * 0.215f), rightW = std::max(460.f, W * 0.30f);
    const float midW = W - leftW - rightW - 2 * gap;
    ImGui::BeginGroup();
    // left: the car and the last 30 s | middle: the map, tyres and car condition | right: standings
    const float carH = std::min(480.f, rest * 0.6f);
    carCard(s, leftW, carH);
    recentCard(a, leftW, rest - carH - sp);
    ImGui::EndGroup();
    ImGui::SameLine(0, gap);
    ImGui::BeginGroup();
    const float bottomH = std::max(330.f, std::min(400.f, rest * 0.47f));
    const float mapH = rest - bottomH - sp;
    mapCard(a, s, midW, mapH);
    carStatusCard(s, midW, bottomH);
    ImGui::EndGroup();
    ImGui::SameLine(0, gap);
    standingsCard(a, s, rightW, rest);
}

// ---- car setup -------------------------------------------------------------------------------------------------------
// The player's setup (CarSetups), what the car is running right now (CarStatus) and the session's assists and rules.
// Online, other players' setups are not sent; this page only shows your own car.
void kvRow(const char *label, const char *value, ImU32 col = C_TEXT)
{
    textCol(C_DIM, "%s", label);
    ImGui::SameLine();
    bool ascii = true; // the number font has no Chinese glyphs
    for (const char *c = value; *c; c++) ascii &= (unsigned char)*c < 0x80;
    auto put = [&] {
        const float w = ImGui::CalcTextSize(value).x, right = ImGui::GetWindowContentRegionMax().x;
        ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), right - w));
        textCol(col, "%s", value);
    };
    if (ascii) withFont(F->num, put);
    else put();
}

const char *actualTyreName(uint8_t c)
{
    switch (c) {
    case 22: return "C6";
    case 16: return "C5";
    case 17: return "C4";
    case 18: return "C3";
    case 19: return "C2";
    case 20: return "C1";
    case 21: return "C0";
    case 7: return "Intermediate";
    case 8: return "Wet";
    case 9: return "Dry (classic)";
    case 10: return "Wet (classic)";
    case 11: return "Super soft (F2)";
    case 12: return "Soft (F2)";
    case 13: return "Medium (F2)";
    case 14: return "Hard (F2)";
    case 15: return "Wet (F2)";
    default: return "-";
    }
}

void pageSetup(app::App &, const Snapshot &s)
{
    const float gap = 12;
    const float W = ImGui::GetContentRegionAvail().x, H = ImGui::GetContentRegionAvail().y;
    const float cw = (W - 2 * gap) / 3, ch = (H - gap) / 2;
    const auto &st = s.setup;
    const auto &cf = s.config;
    const auto &si = s.session;
    char v[64];
    auto f1 = [&](const char *fmt, double x) { std::snprintf(v, sizeof v, fmt, x); return v; };
    auto none = [&](bool ok, const char *what) {
        if (!ok) textCol(alpha(C_DIM, 0.7f), "No %s received yet", what);
        return ok;
    };

    // row 1: the setup
    beginCard("aero", ImVec2(cw, ch), "AERO & TRANSMISSION");
    if (none(st.valid, "setup data (CarSetups packet)")) {
        kvRow("Front wing", f1("%.0f", st.frontWing));
        kvRow("Rear wing", f1("%.0f", st.rearWing));
        if (st.nextFrontWing > 0 && std::lround(st.nextFrontWing) != st.frontWing)
            kvRow("Front wing after next stop", f1("%.0f", st.nextFrontWing), C_YELLOW);
        ImGui::Spacing();
        kvRow("Diff on throttle", f1("%.0f%%", st.onThrottle));
        kvRow("Diff off throttle", f1("%.0f%%", st.offThrottle));
        kvRow("Engine braking", f1("%.0f%%", st.engineBraking));
    }
    endCard();
    ImGui::SameLine(0, gap);
    beginCard("susp", ImVec2(cw, ch), "SUSPENSION & GEOMETRY");
    if (none(st.valid, "setup data (CarSetups packet)")) {
        kvRow("Front suspension", f1("%.0f", st.frontSuspension));
        kvRow("Rear suspension", f1("%.0f", st.rearSuspension));
        kvRow("Front anti-roll bar", f1("%.0f", st.frontAntiRollBar));
        kvRow("Rear anti-roll bar", f1("%.0f", st.rearAntiRollBar));
        kvRow("Front ride height", f1("%.0f", st.frontRideHeight));
        kvRow("Rear ride height", f1("%.0f", st.rearRideHeight));
        ImGui::Spacing();
        kvRow("Front camber", f1("%.2f°", st.frontCamber));
        kvRow("Rear camber", f1("%.2f°", st.rearCamber));
        kvRow("Toe front", f1("%.2f°", st.frontToe));
        kvRow("Toe rear", f1("%.2f°", st.rearToe));
    }
    endCard();
    ImGui::SameLine(0, gap);
    beginCard("brakes", ImVec2(cw, ch), "BRAKES, TYRES & BALLAST");
    if (none(st.valid, "setup data (CarSetups packet)")) {
        kvRow("Brake pressure", f1("%.0f%%", st.brakePressure));
        kvRow("Brake bias (setup)", f1("%.0f%%", st.brakeBias));
        ImGui::Spacing();
        const char *names[4] = {"Pressure FL", "Pressure FR", "Pressure RL", "Pressure RR"};
        const int idx[4] = {2, 3, 0, 1};
        for (int k = 0; k < 4; k++) kvRow(names[k], f1("%.1f psi", st.tyrePressure[idx[k]]));
        ImGui::Spacing();
        kvRow("Ballast", f1("%.0f", st.ballast));
        kvRow("Fuel at start", f1("%.1f kg", st.fuelLoad));
    }
    endCard();

    // row 2: running now, assists, session
    beginCard("running", ImVec2(cw, ch), "RUNNING NOW (adjustable in the race)");
    if (none(cf.valid, "car status (CarStatus packet)")) {
        const bool biasMoved = st.valid && cf.brakeBias != st.brakeBias;
        kvRow("Brake bias", f1("%.0f%%", cf.brakeBias), biasMoved ? C_YELLOW : C_TEXT);
        const char *tc[3] = {"Off", "Medium", "Full"};
        kvRow("Traction control", cf.tractionControl < 3 ? tc[cf.tractionControl] : "?");
        kvRow("ABS", cf.abs ? "On" : "Off");
        const char *mix[4] = {"Lean", "Standard", "Rich", "Max"};
        kvRow("Fuel mix", cf.fuelMix < 4 ? mix[cf.fuelMix] : "?");
        kvRow("Pit limiter", cf.pitLimiter ? "On" : "Off", cf.pitLimiter ? C_BLUE : C_TEXT);
        ImGui::Spacing();
        std::snprintf(v, sizeof v, "%s  %u laps", actualTyreName(cf.actualTyre), unsigned(cf.tyreAge));
        kvRow("Tyre", v);
        kvRow("Fuel capacity", f1("%.1f kg", cf.fuelCapacity));
        std::snprintf(v, sizeof v, "%u / %u rpm", unsigned(cf.maxRPM), unsigned(cf.idleRPM));
        kvRow("Max / idle rpm", v);
        kvRow("Gears", f1("%.0f", cf.maxGears));
        std::snprintf(v, sizeof v, "%.0f + %.0f kW", cf.iceKW, cf.mgukKW);
        kvRow("Power ICE + MGU-K", v);
    }
    endCard();
    ImGui::SameLine(0, gap);
    beginCard("assists", ImVec2(cw, ch), "ASSISTS");
    if (none(si.valid, "session info (Session packet)")) {
        const auto &as = si.assists;
        auto onoff = [](uint8_t x) { return x ? "On" : "Off"; };
        kvRow("Steering assist", onoff(as.steering));
        const char *brk[4] = {"Off", "Low", "Medium", "High"};
        kvRow("Braking assist", as.braking < 4 ? brk[as.braking] : "?");
        const char *gb[4] = {"?", "Manual", "Manual + suggested gear", "Automatic"};
        kvRow("Gearbox", as.gearbox < 4 ? gb[as.gearbox] : "?");
        kvRow("Pit assist", onoff(as.pit));
        kvRow("Pit release assist", onoff(as.pitRelease));
        kvRow("ERS assist", onoff(as.ers));
        kvRow("DRS assist", onoff(as.drs));
        const char *rl[3] = {"Off", "Corners only", "Full"};
        std::snprintf(v, sizeof v, "%s%s", as.racingLine < 3 ? rl[as.racingLine] : "?",
                      as.racingLine ? (as.racingLineType ? " (3D)" : " (2D)") : "");
        kvRow("Dynamic racing line", v);
    }
    endCard();
    ImGui::SameLine(0, gap);
    beginCard("sessioninfo", ImVec2(cw, ch), "SESSION & PIT");
    if (none(si.valid, "session info (Session packet)")) {
        kvRow("SESSION", timing::sessionTypeName(si.sessionType));
        std::snprintf(v, sizeof v, "%s / %s", app::formatDuration(si.timeLeft).c_str(), app::formatDuration(si.duration).c_str());
        kvRow("Left / total time", v);
        kvRow("Pit speed limit", f1("%.0f km/h", si.pitSpeedLimit));
        ImGui::Spacing();
        if (si.pitIdealLap || si.pitLatestLap) {
            std::snprintf(v, sizeof v, "Lap %u ~ lap %u", unsigned(si.pitIdealLap), unsigned(si.pitLatestLap));
            kvRow("Pit window (ideal ~ latest)", v);
            kvRow("Position after the stop", si.pitRejoinPosition ? f1("P%.0f", si.pitRejoinPosition) : "-");
        } else {
            kvRow("Pit window", "-");
        }
        ImGui::Spacing();
        std::snprintf(v, sizeof v, "%d° / %d°", int(si.trackTemp), int(si.airTemp));
        kvRow("Track / air temp", v);
        kvRow("FORECAST", si.forecastAccuracy ? "approx." : "accurate");
    }
    endCard();
}

// ---- track calibration -----------------------------------------------------------------------------------------------
// Recordings -> calibration data files (one per track) -> a calibrated map file per track. Data files from several
// people can be calibrated together; map files can be exported and imported.
// ---- lap comparison ----------------------------------------------------------------------------------------------------
// Two laps from recordings (the same file or two different ones) side by side: track map, sectors, charts. Aligned by
// lap distance (where on track) or by time into the lap.
constexpr ImU32 C_LAP_A = IM_COL32(60, 210, 240, 255);
constexpr ImU32 C_LAP_B = IM_COL32(255, 150, 50, 255);

cmp::Cache g_cmpCache;
struct CmpSlot {
    std::string path;
    int lapNum = -1;
};
CmpSlot g_slot[2];
int g_align = 0;          // 0 by position, 1 by time
float g_cmpCursor = -1;   // shared cursor, metres or seconds
float g_cmpZoom = 1, g_cmpPanX = 0, g_cmpPanZ = 0, g_cmpScale = 1;
// comparison playback: time into lap A (s); the cursor follows it unless the mouse is over a chart
float g_cmpT = 0, g_cmpSpeed = 1;
bool g_cmpPlaying = false;
float g_cmpView0 = 0, g_cmpView1 = 1; // visible part of the charts' x axis, as fractions of the lap
bool g_cmpRDrag = false;
float g_cmpBase = -1; // cursor position from the playback, metres or seconds

const cmp::Lap *findLap(const cmp::File *f, int lapNum)
{
    if (!f) return nullptr;
    for (const auto &l : f->laps)
        if (l.lapNum == lapNum) return &l;
    return nullptr;
}

// File + lap pickers for one slot.
void slotPicker(app::App &a, int i, ImU32 col, float w)
{
    auto &sl = g_slot[i];
    ImGui::PushID(i);
    ImGui::BeginGroup();
    ImDrawList *dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    dl->AddRectFilled(ImVec2(p.x, p.y + 4), ImVec2(p.x + 22, p.y + 26), col, 4);
    dl->AddText(ImVec2(p.x + 6, p.y + 5), IM_COL32(0, 0, 0, 255), i == 0 ? "A" : "B");
    ImGui::Dummy(ImVec2(28, 28));
    ImGui::SameLine();
    const float fileW = w * 0.55f, lapW = w - fileW - 44;
    ImGui::SetNextItemWidth(fileW);
    const std::string fname = sl.path.empty() ? "Choose recording..." : fs::path(sl.path).filename().string();
    if (ImGui::BeginCombo("##file", fname.c_str())) {
        for (const auto &r : a.recordings) {
            std::string label = r.name;
            if (r.info.sessionType >= 0) label += std::string("  · ") + sessionTypeName(r.info.sessionType);
            if (ImGui::Selectable(label.c_str(), r.path == sl.path)) { sl.path = r.path; sl.lapNum = -1; }
        }
        ImGui::Separator();
        if (ImGui::Selectable("Browse other files...")) {
            std::string pth = pickRecording(a.recordingsDir());
            if (!pth.empty()) { sl.path = pth; sl.lapNum = -1; }
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(lapW);
    auto f = sl.path.empty() ? nullptr : g_cmpCache.get(sl.path);
    std::string lapLabel = sl.path.empty() ? "-" : !f ? "Loading..." : !f->error.empty() ? f->error : "Choose lap";
    if (f && f->error.empty()) {
        if (sl.lapNum < 0 && f->bestIdx >= 0) { // default: the fastest lap, or the second fastest for B on the same file
            int pick = f->bestIdx;
            if (i == 1 && g_slot[0].path == sl.path) {
                int second = -1;
                for (int k = 0; k < int(f->laps.size()); k++)
                    if (k != f->bestIdx && f->laps[k].valid && (second < 0 || f->laps[k].lapMs < f->laps[second].lapMs)) second = k;
                if (second >= 0) pick = second;
            }
            sl.lapNum = f->laps[pick].lapNum;
        }
        if (const cmp::Lap *l = findLap(f.get(), sl.lapNum)) {
            char b[64];
            std::snprintf(b, sizeof b, "Lap %d  %s%s", l->lapNum, formatLap(l->lapMs).c_str(), l->valid ? "" : "  invalid");
            lapLabel = b;
        }
    }
    if (ImGui::BeginCombo("##lap", lapLabel.c_str())) {
        if (f && f->error.empty())
            for (int k = 0; k < int(f->laps.size()); k++) {
                const auto &l = f->laps[k];
                char b[80];
                std::snprintf(b, sizeof b, "Lap %2d  %s%s%s", l.lapNum, formatLap(l.lapMs).c_str(), l.valid ? "" : "  invalid",
                              k == f->bestIdx ? "  fastest" : "");
                ImGui::PushStyleColor(ImGuiCol_Text, v4(!l.valid ? C_RED : k == f->bestIdx ? C_PURPLE : C_TEXT));
                if (ImGui::Selectable(b, l.lapNum == sl.lapNum)) sl.lapNum = l.lapNum;
                ImGui::PopStyleColor();
            }
        ImGui::EndCombo();
    }
    if (f && f->error.empty()) {
        const tracks::Shape *sh = tracks::forGame(f->trackId);
        textCol(C_DIM, "      %s · %s", f->driver.empty() ? "?" : f->driver.c_str(), sh ? sh->name.c_str() : "Unknown track");
    } else {
        textCol(C_DIM, " ");
    }
    ImGui::EndGroup();
    ImGui::PopID();
}

// The sample of a lap at the cursor (distance or time depending on alignment).
timing::Sample at(const cmp::Lap &l, float x)
{
    return g_align == 0 ? l.trace.at(x) : cmp::sampleAtTime(l.trace, x);
}

std::vector<ImVec2> cmpPoints(const cmp::Lap &l, Getter g, int maxPts)
{
    std::vector<ImVec2> v;
    const auto &s = l.trace.s;
    if (s.empty()) return v;
    const float t0 = s.front().t;
    const size_t step = std::max<size_t>(1, s.size() / size_t(std::max(1, maxPts)));
    for (size_t i = 0; i < s.size(); i += step) v.push_back(ImVec2(g_align == 0 ? s[i].dist : s[i].t - t0, g(s[i])));
    return v;
}

void cmpMap(const cmp::File &fa, const cmp::Lap &A, const cmp::Lap &B, float w, float h)
{
    beginCard("cmpmap", ImVec2(w, h), nullptr);
    ImDrawList *dl = ImGui::GetWindowDrawList();
    // outline aligned to lap A once per (file, lap)
    static std::string geoKey;
    static tracks::Geometry geo;
    const std::string key = fa.path + "#" + std::to_string(A.lapNum);
    if (key != geoKey) {
        geoKey = key;
        geo = {};
        if (const tracks::TnrTrack *t = fa.trackId >= 0 ? tracks::tnrForGame(fa.trackId) : nullptr) {
            geo = tracks::fromTnr(*t, fa.trackLength > 0 ? fa.trackLength : A.trace.s.back().dist);
        } else if (const tracks::Shape *sh = tracks::forGame(fa.trackId)) {
            std::vector<tracks::DrivenPoint> pts;
            for (const auto &q : A.trace.s) pts.push_back({q.dist, q.x, q.z});
            geo = tracks::align(*sh, pts, fa.trackLength > 0 ? fa.trackLength : A.trace.s.back().dist);
        }
    }
    textCol(C_DIM, "TRACK");
    ImGui::SameLine();
    textCol(alpha(C_DIM, 0.75f), "%s", geo.ok ? geo.name.c_str() : "Driving lines (no track data available)");
    for (int sl = 0; sl < 2; sl++) { // legend: a short line in each lap's colour
        ImGui::SameLine(0, 14);
        const ImVec2 q = ImGui::GetCursorScreenPos();
        const float ly = q.y + ImGui::GetTextLineHeight() / 2;
        ImGui::GetWindowDrawList()->AddLine(ImVec2(q.x, ly), ImVec2(q.x + 18, ly), sl == 0 ? C_LAP_A : C_LAP_B, 3.f);
        ImGui::Dummy(ImVec2(22, 1));
        ImGui::SameLine(0, 2);
        textCol(sl == 0 ? C_LAP_A : C_LAP_B, sl == 0 ? "A" : "B");
    }

    const ImVec2 o = ImGui::GetCursorScreenPos();
    const ImVec2 av = ImGui::GetContentRegionAvail();
    float x0 = 1e9f, x1 = -1e9f, z0 = 1e9f, z1 = -1e9f;
    auto grow = [&](float x, float z) { x0 = std::min(x0, x); x1 = std::max(x1, x); z0 = std::min(z0, z); z1 = std::max(z1, z); };
    if (geo.ok) for (size_t i = 0; i < geo.lx.size(); i++) { grow(geo.lx[i], geo.lz[i]); grow(geo.rx[i], geo.rz[i]); }
    for (const auto &q : A.trace.s) grow(q.x, q.z);
    const float pad = 16;
    const float k0 = std::min((av.x - 2 * pad) / std::max(1.f, x1 - x0), (av.y - 2 * pad) / std::max(1.f, z1 - z0));
    const float k = k0 * g_cmpZoom;
    g_cmpScale = k;
    // zoomed in: centre on the cursor point of lap A when there is one
    float cx = (x0 + x1) / 2, cz = (z0 + z1) / 2;
    if (g_cmpZoom > 1.01f && g_cmpCursor >= 0) {
        const auto q = at(A, g_cmpCursor);
        cx = q.x;
        cz = q.z;
    }
    cx += g_cmpPanX;
    cz += g_cmpPanZ;
    const float midX = o.x + av.x / 2, midY = o.y + av.y / 2;
    auto map = [&](float x, float z) { return ImVec2(midX + (x - cx) * k, midY + (z - cz) * k); }; // down = +z
    dl->PushClipRect(o, ImVec2(o.x + av.x, o.y + av.y), true);
    if (geo.ok) { // a scanned map shows the scan only
        if (geo.runs.empty() && geo.bands.empty()) drawOutline(dl, geo, map);
        drawSurface(dl, geo, map, k, o, ImVec2(o.x + av.x, o.y + av.y));
        if (!geo.runs.empty() || !geo.bands.empty()) { drawKerbs(dl, geo, map); drawEdges(dl, geo, map); }
        drawZones(dl, geo, map);
        surfaceLegend(dl, geo, ImVec2(o.x + 8, o.y + av.y - 2 * ImGui::GetTextLineHeight() - 8)); // above the hover hint
    }
    // both driving lines; B underneath
    for (int sl = 1; sl >= 0; sl--) {
        const auto &L = sl == 0 ? A : B;
        std::vector<ImVec2> pts;
        const size_t step = g_cmpZoom > 3.f ? 1 : std::max<size_t>(1, L.trace.s.size() / 1500);
        for (size_t i = 0; i < L.trace.s.size(); i += step) pts.push_back(map(L.trace.s[i].x, L.trace.s[i].z));
        dl->AddPolyline(pts.data(), int(pts.size()), alpha(sl == 0 ? C_LAP_A : C_LAP_B, 0.95f), 0, g_cmpZoom > 3.f ? 2.f : 2.5f);
    }
    // sector boundaries and the cursor positions of both cars
    auto markDist = [&](float dist, const char *label) {
        if (dist <= 0) return;
        const auto q = A.trace.at(dist);
        const ImVec2 c = map(q.x, q.z);
        dl->AddCircleFilled(c, 4, C_TEXT);
        dl->AddText(ImVec2(c.x + 6, c.y - 8), C_TEXT, label);
    };
    markDist(1.f, "S/F");
    markDist(fa.s2, "S2");
    markDist(fa.s3, "S3");
    if (g_cmpCursor >= 0) {
        for (int s = 1; s >= 0; s--) {
            const auto &L = s == 0 ? A : B;
            const auto q = at(L, g_cmpCursor);
            // heading: along the driving line a couple of metres on
            const auto q2 = L.trace.at(q.dist + 3.f);
            const ImVec2 c = map(q.x, q.z), c2 = map(q2.x, q2.z);
            carMarker(dl, c, ImVec2(c2.x - c.x, c2.y - c.y), 8, s == 0 ? C_LAP_A : C_LAP_B, IM_COL32(0, 0, 0, 255));
        }
    }
    // gap at the cursor, top right: by position the time gap, by time the distance gap
    if (g_cmpCursor >= 0) {
        char val[32] = "--";
        ImU32 col = C_DIM;
        const char *label;
        if (g_align == 0) {
            label = "Time delta (B - A)";
            const float ta = A.trace.timeAt(g_cmpCursor), tb = B.trace.timeAt(g_cmpCursor);
            if (ta >= 0 && tb >= 0) {
                const float d = tb - ta;
                std::snprintf(val, sizeof val, "%+.3f s", d);
                col = d > 0 ? C_LAP_A : C_LAP_B; // the colour of whoever is ahead
            }
        } else {
            label = "Distance delta (A - B)";
            const float da = cmp::distAtTime(A.trace, g_cmpCursor), db = cmp::distAtTime(B.trace, g_cmpCursor);
            if (da >= 0 && db >= 0) {
                const float d = da - db;
                std::snprintf(val, sizeof val, "%+.1f m", d);
                col = d > 0 ? C_LAP_A : C_LAP_B;
            }
        }
        const float fs = fsz(F->numMid);
        const ImVec2 vs = F->numMid->CalcTextSizeA(fs, FLT_MAX, 0, val);
        const ImVec2 ls = ImGui::CalcTextSize(label);
        const float bw = std::max(vs.x, ls.x) + 20, bx = o.x + av.x - bw - 6, by = o.y + 6;
        dl->AddRectFilled(ImVec2(bx, by), ImVec2(bx + bw, by + ls.y + fs + 14), alpha(C_BG, 0.85f), 6);
        dl->AddText(ImVec2(bx + bw - ls.x - 10, by + 5), C_DIM, label);
        dl->AddText(F->numMid, fs, ImVec2(bx + bw - vs.x - 10, by + ls.y + 7), col, val);
    }
    dl->PopClipRect();
    ImGui::SetNextItemAllowOverlap();
    ImGui::InvisibleButton("cmpmapin", av, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    const bool hov = ImGui::IsItemHovered();
    if (hov && ImGui::GetIO().MouseWheel != 0)
        g_cmpZoom = std::clamp(g_cmpZoom * std::pow(1.25f, ImGui::GetIO().MouseWheel), 1.f, 30.f);
    if (ImGui::IsItemActive() && g_cmpScale > 0) {
        g_cmpPanX -= ImGui::GetIO().MouseDelta.x / g_cmpScale;
        g_cmpPanZ -= ImGui::GetIO().MouseDelta.y / g_cmpScale;
    }
    if (hov && ImGui::IsMouseDoubleClicked(0)) g_cmpZoom = 1, g_cmpPanX = g_cmpPanZ = 0;
    if (hov) dl->AddText(ImVec2(o.x + 4, o.y + av.y - ImGui::GetTextLineHeight() - 2), alpha(C_DIM, 0.7f),
                         "Wheel: zoom at the cursor · L/R drag: pan · Double-click: fit");
    endCard();
}

void cmpSectors(const cmp::File &fa, const cmp::File &fb, const cmp::Lap &A, const cmp::Lap &B, float w, float h)
{
    beginCard("cmpsec", ImVec2(w, h), "SECTORS (delta = B - A, positive = B slower)");
    // sector times: the game's lap history when known, otherwise from the traces at the sector distances
    auto sectors = [](const cmp::File &f, const cmp::Lap &l) {
        std::array<float, 3> s{};
        if (l.sectorMs[0] && l.sectorMs[1] && l.sectorMs[2]) {
            for (int k = 0; k < 3; k++) s[k] = l.sectorMs[k] / 1000.f;
            return s;
        }
        const float t2 = f.s2 > 0 ? l.trace.timeAt(f.s2) : -1, t3 = f.s3 > 0 ? l.trace.timeAt(f.s3) : -1;
        const float lap = l.lapMs / 1000.f;
        if (t2 > 0 && t3 > t2) s = {t2, t3 - t2, lap - t3};
        return s;
    };
    const auto sa = sectors(fa, A), sb = sectors(fb, B);
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(8, 6));
    if (ImGui::BeginTable("sec", 5, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch, 0.6f);
        ImGui::TableSetupColumn("A", ImGuiTableColumnFlags_WidthStretch, 1.f);
        ImGui::TableSetupColumn("B", ImGuiTableColumnFlags_WidthStretch, 1.f);
        ImGui::TableSetupColumn("Delta", ImGuiTableColumnFlags_WidthStretch, 0.9f);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch, 1.4f);
        ImGui::PushStyleColor(ImGuiCol_Text, v4(C_DIM));
        ImGui::TableHeadersRow();
        ImGui::PopStyleColor();
        float maxAbs = 0.05f;
        for (int k = 0; k < 3; k++)
            if (sa[k] > 0 && sb[k] > 0) maxAbs = std::max(maxAbs, std::fabs(sb[k] - sa[k]));
        auto row = [&](const char *name, float ta, float tb, bool lap) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(name);
            ImGui::PushFont(F->num);
            auto fmt = [&](float t) { return t <= 0 ? std::string("-") : lap ? formatLap(uint32_t(t * 1000 + 0.5f)) : formatSector(uint32_t(t * 1000 + 0.5f)); };
            ImGui::TableNextColumn();
            textCol(ta > 0 && tb > 0 && ta <= tb ? C_LAP_A : C_TEXT, "%s", fmt(ta).c_str());
            ImGui::TableNextColumn();
            textCol(ta > 0 && tb > 0 && tb < ta ? C_LAP_B : C_TEXT, "%s", fmt(tb).c_str());
            ImGui::TableNextColumn();
            if (ta > 0 && tb > 0) textCol(tb > ta ? C_LAP_A : C_LAP_B, "%+.3f", tb - ta);
            else textCol(C_DIM, "-");
            ImGui::PopFont();
            ImGui::TableNextColumn();
            if (ta > 0 && tb > 0 && !lap) { // bar from the middle towards the faster lap's side
                const ImVec2 q = ImGui::GetCursorScreenPos();
                const float bw = ImGui::GetContentRegionAvail().x, mid = q.x + bw / 2, y = q.y + 5;
                const float d = std::clamp((tb - ta) / maxAbs, -1.f, 1.f) * bw / 2;
                ImDrawList *dl = ImGui::GetWindowDrawList();
                dl->AddLine(ImVec2(mid, y - 4), ImVec2(mid, y + 12), C_DIM);
                dl->AddRectFilled(ImVec2(std::min(mid, mid - d), y), ImVec2(std::max(mid, mid - d), y + 8), d > 0 ? C_LAP_A : C_LAP_B, 2);
                ImGui::Dummy(ImVec2(bw, 16));
            }
        };
        row("S1", sa[0], sb[0], false);
        row("S2", sa[1], sb[1], false);
        row("S3", sa[2], sb[2], false);
        row("Lap time", A.lapMs / 1000.f, B.lapMs / 1000.f, true);
        ImGui::EndTable();
    }
    ImGui::PopStyleVar();
    ImGui::Spacing();
    // top speed and minimum speeds summary
    auto vmax = [](const cmp::Lap &l) { float m = 0; for (const auto &q : l.trace.s) m = std::max(m, q.speed); return m; };
    textCol(C_DIM, "Top speed");
    ImGui::SameLine();
    withFont(F->num, [&] {
        textCol(C_LAP_A, "%.0f", vmax(A));
        ImGui::SameLine();
        textCol(C_DIM, "/");
        ImGui::SameLine();
        textCol(C_LAP_B, "%.0f km/h", vmax(B));
    });
    endCard();
}

void cmpCharts(const cmp::File &fa, const cmp::Lap &A, const cmp::Lap &B, float w, float h)
{
    // scrolls when the open charts don't fit; the wheel over a chart zooms it, so the mouse scroll is handled below
    // (wheel over a header or gap, Shift+wheel anywhere, or the scrollbar)
    ImGui::PushStyleColor(ImGuiCol_ChildBg, v4(C_PANEL));
    ImGui::PushStyleColor(ImGuiCol_Border, v4(C_BORDER));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 6.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14, 10));
    ImGui::BeginChild("cmpcharts", ImVec2(w, h), ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding,
                      ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);
    const bool pos = g_align == 0;
    const float xa = pos ? A.trace.s.back().dist : A.trace.s.back().t - A.trace.s.front().t;
    const float xb = pos ? B.trace.s.back().dist : B.trace.s.back().t - B.trace.s.front().t;
    const float xmax = std::max(xa, xb);
    // the visible window: wheel zooms around the mouse, right drag pans, double click shows the whole lap
    static int lastAlign = -1;
    if (lastAlign != g_align) { g_cmpView0 = 0; g_cmpView1 = 1; lastAlign = g_align; }
    const float v0 = g_cmpView0 * xmax, v1 = g_cmpView1 * xmax;
    float plotX0 = 0, plotW = 1; // screen span of the plots' data area, for the mouse
    (void)fa;
    // y range: Fixed (natural bounds), Auto (the visible data, padded), Sym (symmetric around 0 over the visible data)
    enum Range { Fixed, Auto, Sym };
    struct Ch {
        const char *name;
        Getter g;
        float y0, y1;
        const char *fmt;
        Range range;
    };
    static const Ch chans[] = {
        {"SPEED km/h", [](const Sample &x) { return x.speed; }, 0, 350, "%.0f", Auto},
        {"Throttle %", [](const Sample &x) { return x.throttle * 100; }, 0, 100, "%.0f", Fixed},
        {"Brake %", [](const Sample &x) { return x.brake * 100; }, 0, 100, "%.0f", Fixed},
        {"Gear", [](const Sample &x) { return float(x.gear); }, 0, 8, "%.0f", Fixed},
        {"RPM", [](const Sample &x) { return x.rpm; }, 4000, 12500, "%.0f", Auto},
        {"STEER", [](const Sample &x) { return x.steer * 100; }, -100, 100, "%.0f", Sym},
    };
    constexpr int NCH = 7; // the six above and the delta
    static bool fold[NCH] = {};
    // folded charts keep a header row; the open ones share the rest of the height, but never below MIN_PH: then
    // the card scrolls
    const float headH = 20, MIN_PH = 120;
    int open = 0;
    for (bool f : fold) open += !f;
    const float ph = open ? std::max(MIN_PH, (ImGui::GetWindowHeight() - 20 - NCH * (headH + 4) - open * 2.f) / float(open)) : MIN_PH;
    auto header = [&](int i, const char *name) {
        char b[64];
        std::snprintf(b, sizeof b, "%s  %s##fold%d", fold[i] ? "+" : "-", name, i);
        ImGui::PushStyleColor(ImGuiCol_Text, v4(C_DIM));
        if (ImGui::Selectable(b, false, 0, ImVec2(0, headH - 4))) fold[i] = !fold[i];
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(fold[i] ? "Show chart" : "Fold chart");
        return !fold[i];
    };
    auto rangeOf = [&](const std::vector<ImVec2> &a, const std::vector<ImVec2> *b, float &lo, float &hi) {
        lo = 1e30f, hi = -1e30f;
        for (const auto *v : {&a, b})
            if (v)
                for (const auto &q : *v)
                    if (q.x >= v0 && q.x <= v1) { lo = std::min(lo, q.y); hi = std::max(hi, q.y); }
        return lo <= hi;
    };
    // enough points for the zoomed window
    const int maxPts = int(ImGui::GetContentRegionAvail().x / std::max(0.02f, g_cmpView1 - g_cmpView0));
    bool anyHover = false;
    float hover = -1;
    auto cursorLine = [&](const Plot &p) {
        if (g_cmpCursor < 0) return;
        ImVec2 a0 = p.map(g_cmpCursor, p.y0), a1 = p.map(g_cmpCursor, p.y1);
        if (a0.x >= p.p0.x && a0.x <= p.p1.x) p.dl->AddLine(ImVec2(a0.x, a1.y), a0, alpha(C_TEXT, 0.5f));
    };
    int ci = 0;
    for (const auto &ch : chans) {
        const int idx = ci++;
        if (!header(idx, ch.name)) continue;
        const auto pa = cmpPoints(A, ch.g, maxPts), pb = cmpPoints(B, ch.g, maxPts);
        float y0 = ch.y0, y1 = ch.y1, lo, hi;
        if (ch.range != Fixed && rangeOf(pa, &pb, lo, hi)) {
            if (ch.range == Sym) {
                const float m = std::max({std::fabs(lo), std::fabs(hi), 5.f}) * 1.1f;
                y0 = -m, y1 = m;
            } else {
                const float pad = std::max((hi - lo) * 0.08f, 5.f);
                y0 = lo - pad, y1 = hi + pad;
            }
        }
        Plot p = beginPlot(ch.name, ph, v0, v1, y0, y1, "");
        plotX0 = p.p0.x;
        plotW = p.p1.x - p.p0.x;
        anyHover |= p.hovered;
        plotLine(p, pb, alpha(C_LAP_B, 0.95f), 1.6f);
        plotLine(p, pa, alpha(C_LAP_A, 0.95f), 1.6f);
        if (p.active) hover = std::clamp(p.unmapX(ImGui::GetIO().MousePos.x), v0, v1);
        cursorLine(p);
        if (g_cmpCursor >= 0) {
            char va[24], vb[24], b[64];
            std::snprintf(va, sizeof va, ch.fmt, ch.g(at(A, g_cmpCursor)));
            std::snprintf(vb, sizeof vb, ch.fmt, ch.g(at(B, g_cmpCursor)));
            std::snprintf(b, sizeof b, "%s  |  %s", va, vb);
            ImVec2 ts = ImGui::CalcTextSize(b);
            p.dl->AddRectFilled(ImVec2(p.p1.x - ts.x - 16, p.p0.y + 4), ImVec2(p.p1.x - 4, p.p0.y + ts.y + 10), alpha(C_BG, 0.85f), 4);
            const float wa = ImGui::CalcTextSize(va).x;
            p.dl->AddText(ImVec2(p.p1.x - ts.x - 10, p.p0.y + 7), C_LAP_A, va);
            p.dl->AddText(ImVec2(p.p1.x - ts.x - 10 + wa, p.p0.y + 7), C_DIM, "  |  ");
            p.dl->AddText(ImVec2(p.p1.x - ts.x - 10 + wa + ImGui::CalcTextSize("  |  ").x, p.p0.y + 7), C_LAP_B, vb);
        }
        ImGui::Dummy(ImVec2(0, 2));
    }
    // delta: by position = time B - time A at the same distance; by time = metres A is ahead of B
    if (header(NCH - 1, pos ? "Time delta s (B - A, positive = B slower)" : "Distance delta m (positive = A ahead)")) {
        std::vector<ImVec2> pts;
        float lo = pos ? -0.2f : -5.f, hi = -lo;
        const auto &s = B.trace.s;
        const size_t step = std::max<size_t>(1, s.size() / size_t(std::max(1, maxPts)));
        for (size_t i = 0; i < s.size(); i += step) {
            float x, d;
            if (pos) {
                const float ta = A.trace.timeAt(s[i].dist);
                if (ta < 0) continue;
                x = s[i].dist;
                d = (s[i].t - s.front().t) - ta;
            } else {
                x = s[i].t - s.front().t;
                const float da = cmp::distAtTime(A.trace, x);
                if (da < 0) continue;
                d = da - s[i].dist;
            }
            pts.push_back(ImVec2(x, d));
            lo = std::min(lo, d);
            hi = std::max(hi, d);
        }
        // light smoothing: the two traces are sampled at different moments, which shows up as jitter
        if (pts.size() > 9) {
            std::vector<ImVec2> sm(pts.size());
            for (size_t i = 0; i < pts.size(); i++) {
                float sum = 0;
                int n = 0;
                for (size_t j = i >= 4 ? i - 4 : 0; j <= std::min(pts.size() - 1, i + 4); j++, n++) sum += pts[j].y;
                sm[i] = ImVec2(pts[i].x, sum / float(n));
            }
            pts.swap(sm);
        }
        // symmetric around 0 over what is visible (after smoothing), so the line never leaves the chart
        float wl, wh;
        if (rangeOf(pts, nullptr, wl, wh)) lo = wl, hi = wh;
        const float m = std::max({std::fabs(lo), std::fabs(hi), pos ? 0.05f : 1.f}) * 1.15f;
        Plot p = beginPlot("cmpdelta", ph, v0, v1, -m, m, "");
        p.dl->AddLine(ImVec2(p.p0.x, p.map(0, 0).y), ImVec2(p.p1.x, p.map(0, 0).y), alpha(C_TEXT, 0.4f));
        anyHover |= p.hovered;
        plotLine(p, pts, C_YELLOW, 2.f);
        if (p.active) hover = std::clamp(p.unmapX(ImGui::GetIO().MousePos.x), v0, v1);
        cursorLine(p);
        if (g_cmpCursor >= 0 && !pts.empty()) {
            auto it = std::lower_bound(pts.begin(), pts.end(), g_cmpCursor, [](const ImVec2 &q, float v) { return q.x < v; });
            if (it != pts.end()) {
                char b[32];
                std::snprintf(b, sizeof b, pos ? "%+.3f s" : "%+.1f m", it->y);
                ImVec2 ts = ImGui::CalcTextSize(b);
                p.dl->AddText(ImVec2(p.p1.x - ts.x - 10, p.p0.y + 7), C_YELLOW, b);
            }
        }
    }
    {
        ImGuiIO &io = ImGui::GetIO();
        const float span = g_cmpView1 - g_cmpView0;
        const bool shift = io.KeyShift;
        if (io.MouseWheel != 0 && ImGui::IsWindowHovered() && (shift || !anyHover))
            ImGui::SetScrollY(ImGui::GetScrollY() - io.MouseWheel * 80.f);
        else if (anyHover && io.MouseWheel != 0 && plotW > 1) {
            const float mf = g_cmpView0 + std::clamp((io.MousePos.x - plotX0) / plotW, 0.f, 1.f) * span;
            const float ns = std::clamp(span * std::pow(0.8f, io.MouseWheel), 0.01f, 1.f);
            g_cmpView0 = mf - (mf - g_cmpView0) * ns / span;
            g_cmpView1 = g_cmpView0 + ns;
        }
        if (anyHover && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) g_cmpRDrag = true;
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Right)) g_cmpRDrag = false;
        if (g_cmpRDrag && plotW > 1) {
            const float d = -io.MouseDelta.x / plotW * span;
            g_cmpView0 += d;
            g_cmpView1 += d;
        }
        if (anyHover && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) g_cmpView0 = 0, g_cmpView1 = 1;
        // keep the window inside the lap
        const float sp = g_cmpView1 - g_cmpView0;
        if (g_cmpView0 < 0) { g_cmpView0 = 0; g_cmpView1 = sp; }
        if (g_cmpView1 > 1) { g_cmpView1 = 1; g_cmpView0 = 1 - sp; }
        if (anyHover && sp < 0.999f)
            ImGui::GetWindowDrawList()->AddText(ImVec2(plotX0 + 4, ImGui::GetWindowPos().y + ImGui::GetWindowHeight() - ImGui::GetTextLineHeight() - 8),
                                                alpha(C_DIM, 0.8f), "Wheel: zoom · Right drag: pan · Shift+wheel: scroll charts · Double-click: whole lap");
    }

    // pressing or dragging on a chart moves the playback position there
    if (hover >= 0) {
        const float t = pos ? A.trace.timeAt(std::min(hover, A.trace.s.back().dist)) : hover;
        if (t >= 0) g_cmpT = t;
        g_cmpPlaying = false;
        g_cmpBase = hover;
        ImGui::SetTooltip(pos ? "Distance %.0f m" : "Time %.2f s", hover);
    }
    g_cmpCursor = g_cmpBase;
    endCard();
}

// Playback of the two laps: both cars move along their laps together.
void cmpTransport(const cmp::File &fa, const cmp::Lap &A, const cmp::Lap &B)
{
    const float lenT = A.trace.s.back().t - A.trace.s.front().t;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, v4(C_SIDE));
    ImGui::BeginChild("cmptransport", ImVec2(0, 58), ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar);
    ImGui::PopStyleColor();
    ImGui::SetCursorPosY(12);
    if (accentButton(g_cmpPlaying ? "Pause" : "Play", ImVec2(72, 30), C_LAP_A)) {
        if (!g_cmpPlaying && g_cmpT >= lenT - 0.01f) g_cmpT = 0;
        g_cmpPlaying = !g_cmpPlaying;
    }
    ImGui::SameLine();
    ImGui::SetCursorPosY(12 + (30 - fsz(F->num)) / 2);
    const auto q = A.trace.at(cmp::sampleAtTime(A.trace, g_cmpT).dist);
    withFont(F->num, [&] {
        ImGui::Text("%d:%06.3f / %s", int(g_cmpT) / 60, std::fmod(g_cmpT, 60.f), formatLap(A.lapMs).c_str());
        ImGui::SameLine();
        textCol(C_DIM, "%.0f m", q.dist);
    });
    ImGui::SameLine();
    static const float speeds[] = {0.25f, 0.5f, 1.f, 2.f, 4.f};
    static const char *names[] = {"0.25x", "0.5x", "1x", "2x", "4x"};
    int cur = 2;
    for (int i = 0; i < 5; i++)
        if (std::fabs(speeds[i] - g_cmpSpeed) < 1e-3f) cur = i;
    ImGui::SetCursorPosY(12 + (30 - ImGui::GetFrameHeight()) / 2);
    ImGui::SetNextItemWidth(84);
    if (ImGui::Combo("##cmpspeed", &cur, names, 5)) g_cmpSpeed = speeds[cur];
    ImGui::SameLine(0, 20);
    ImGui::SetCursorPosY(12);
    // bar over lap A's time, sector boundaries marked, drag to scrub
    ImDrawList *dl = ImGui::GetWindowDrawList();
    const ImVec2 c = ImGui::GetCursorScreenPos();
    const float w = ImGui::GetContentRegionAvail().x, h = 30, cy = c.y + h / 2;
    ImGui::InvisibleButton("cmpbar", ImVec2(w, h));
    if (ImGui::IsItemActive()) {
        g_cmpT = std::clamp((ImGui::GetIO().MousePos.x - c.x) / w, 0.f, 1.f) * lenT;
        g_cmpPlaying = false;
    }
    const float frac = lenT > 0 ? g_cmpT / lenT : 0;
    dl->AddRectFilled(ImVec2(c.x, cy - 3), ImVec2(c.x + w, cy + 3), C_PANEL2, 3);
    dl->AddRectFilled(ImVec2(c.x, cy - 3), ImVec2(c.x + w * frac, cy + 3), C_LAP_A, 3);
    auto tick = [&](float dist, const char *label) {
        if (dist <= 0) return;
        const float t = A.trace.timeAt(dist);
        if (t < 0) return;
        const float x = c.x + w * t / lenT;
        dl->AddLine(ImVec2(x, cy - 8), ImVec2(x, cy + 8), alpha(C_TEXT, 0.5f));
        dl->AddText(ImVec2(x + 3, cy + 4), C_DIM, label);
    };
    tick(fa.s2, "S2");
    tick(fa.s3, "S3");
    // where lap B is at the same moment, as a second marker
    if (const float db = cmp::distAtTime(B.trace, g_cmpT); db >= 0) {
        const float ta = A.trace.timeAt(db);
        if (ta >= 0) dl->AddCircleFilled(ImVec2(c.x + w * ta / lenT, cy), 5, C_LAP_B);
    }
    dl->AddCircleFilled(ImVec2(c.x + w * frac, cy), ImGui::IsItemActive() ? 8.f : 6.5f, IM_COL32_WHITE);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("White dot = A, orange dot = where B was on A's lap at the same moment");
    ImGui::EndChild();
}

void pageCompare(app::App &a)
{
    static bool init = false;
    if (!init) { // defaults: the recording being replayed, else the newest one
        init = true;
        if (a.recordings.empty()) a.scanRecordings();
        std::string def = a.source() == Source::Replay && a.replay.isOpen() ? a.replay.path()
                          : a.recordings.empty()                            ? std::string()
                                                                            : a.recordings.front().path;
        g_slot[0].path = g_slot[1].path = def;
    }
    const float W = ImGui::GetContentRegionAvail().x, gap = 12;

    beginCard("cmpsel", ImVec2(W, 84), nullptr);
    const float alignW = 230, slotW = (ImGui::GetContentRegionAvail().x - alignW - 2 * gap) / 2;
    slotPicker(a, 0, C_LAP_A, slotW);
    ImGui::SameLine(0, gap);
    slotPicker(a, 1, C_LAP_B, slotW);
    ImGui::SameLine(0, gap);
    ImGui::BeginGroup();
    textCol(C_DIM, "Align by");
    ImGui::RadioButton("Position", &g_align, 0);
    ImGui::SameLine();
    ImGui::RadioButton("Time", &g_align, 1);
    ImGui::EndGroup();
    endCard();
    ImGui::Dummy(ImVec2(0, gap - ImGui::GetStyle().ItemSpacing.y));

    auto fa = g_slot[0].path.empty() ? nullptr : g_cmpCache.get(g_slot[0].path);
    auto fb = g_slot[1].path.empty() ? nullptr : g_cmpCache.get(g_slot[1].path);
    const cmp::Lap *A = findLap(fa.get(), g_slot[0].lapNum), *B = findLap(fb.get(), g_slot[1].lapNum);
    if (!A || !B) {
        ImVec2 av = ImGui::GetContentRegionAvail();
        ImGui::SetCursorPos(ImVec2(ImGui::GetCursorPosX() + av.x / 2 - 220, ImGui::GetCursorPosY() + av.y / 2 - 60));
        beginCard("cmpempty", ImVec2(440, 120));
        withFont(F->title, [] { ImGui::TextUnformatted("Choose the two laps to compare"); });
        ImGui::Spacing();
        if ((!g_slot[0].path.empty() && !fa) || (!g_slot[1].path.empty() && !fb))
            ImGui::TextWrapped("Reading the recording... (large files take a few seconds)");
        else
            ImGui::TextWrapped("Above, choose a recording and a lap for both A and B: two laps of one file, or laps from different recordings (someone else's, say).");
        endCard();
        return;
    }
    // playback clock
    {
        const float lenT = A->trace.s.back().t - A->trace.s.front().t;
        if (g_cmpPlaying) {
            g_cmpT += ImGui::GetIO().DeltaTime * g_cmpSpeed;
            if (g_cmpT >= lenT) { g_cmpT = lenT; g_cmpPlaying = false; }
        }
        g_cmpT = std::clamp(g_cmpT, 0.f, lenT);
        g_cmpBase = g_align == 0 ? cmp::sampleAtTime(A->trace, g_cmpT).dist : g_cmpT;
    }
    if (fa->trackId >= 0 && fb->trackId >= 0 && fa->trackId != fb->trackId) {
        textCol(C_ORANGE, "Note: A and B are not on the same track; comparing them means nothing.");
    }
    const float H = ImGui::GetContentRegionAvail().y - 58 - ImGui::GetStyle().ItemSpacing.y;
    const float leftW = std::max(420.f, W * 0.36f);
    ImGui::BeginGroup();
    const float secH = 250;
    cmpMap(*fa, *A, *B, leftW, H - secH - ImGui::GetStyle().ItemSpacing.y);
    cmpSectors(*fa, *fb, *A, *B, leftW, secH);
    ImGui::EndGroup();
    ImGui::SameLine(0, gap);
    cmpCharts(*fa, *A, *B, W - leftW - gap, H);
    cmpTransport(*fa, *A, *B);
}

// ---- capture ---------------------------------------------------------------------------------------------------------
void sourceCard(app::App &a, const char *id, Source src, const char *title, const char *desc, float w, ImU32 col)
{
    const bool sel = a.source() == src;
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::PushID(id);
    bool click = ImGui::InvisibleButton("src", ImVec2(w, 104));
    bool hov = ImGui::IsItemHovered();
    ImGui::PopID();
    ImDrawList *dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, ImVec2(p.x + w, p.y + 104), sel ? alpha(col, 0.12f) : hov ? C_PANEL2 : C_PANEL, 8);
    dl->AddRect(p, ImVec2(p.x + w, p.y + 104), sel ? col : C_BORDER, 8, 0, sel ? 2.f : 1.f);
    dl->AddCircleFilled(ImVec2(p.x + 22, p.y + 26), 6, sel ? col : alpha(col, 0.4f));
    ImGui::PushFont(F->title);
    dl->AddText(ImVec2(p.x + 38, p.y + 12), C_TEXT, title);
    ImGui::PopFont();
    dl->AddText(nullptr, 0, ImVec2(p.x + 18, p.y + 48), C_DIM, desc, nullptr, w - 36);
    if (sel) {
        ImVec2 ts = ImGui::CalcTextSize("IN USE");
        dl->AddText(ImVec2(p.x + w - ts.x - 16, p.y + 16), col, "IN USE");
    }
    if (click && !sel) {
        if (src == Source::Live) a.useLive();
        else if (src == Source::Sim) a.useSim();
        else g_page = REPLAY;
    }
}

void pageCapture(app::App &a)
{
    const float W = ImGui::GetContentRegionAvail().x, gap = 12;
    const float cw = (W - 2 * gap) / 3;
    char live[160];
    std::snprintf(live, sizeof live, "Receives the UDP telemetry F1 25 sends (port %u). Works online and offline; the game is not touched.", a.cfg.port);
    sourceCard(a, "live", Source::Live, "Live", live, cw, C_GREEN);
    ImGui::SameLine(0, gap);
    sourceCard(a, "sim", Source::Sim, "Simulation", "Generated data for when there is no game: 20 cars, packets exactly like the game's.", cw, C_BLUE);
    ImGui::SameLine(0, gap);
    sourceCard(a, "rep", Source::Replay, "Replay", "Plays a recorded .f1tr file: pause, seek, change speed. Choose the file on the Replay page.", cw,
               C_ORANGE);
    ImGui::Dummy(ImVec2(0, gap - 4));

    const float H = ImGui::GetContentRegionAvail().y;
    const float leftW = (W - gap) * 0.5f;
    // status + packet table
    beginScrollCard("status", ImVec2(leftW, H), "RECEIVING");
    ImGui::TextWrapped("%s", a.status().c_str());
    const auto st = a.hub.stats();
    ImGui::Spacing();
    withFont(F->numMid, [&] { ImGui::Text("%.0f", a.packetRate()); });
    ImGui::SameLine();
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10);
    textCol(C_DIM, "pkt/s    %llu packets in all, %llu invalid", (unsigned long long)st.received, (unsigned long long)st.invalid);
    if (a.source() == Source::Live) {
        const double age = a.secondsSinceData();
        if (age > 1e5 || st.received == 0) textCol(C_DIM, "No data yet");
        else textCol(age < 2 ? C_GREEN : C_ORANGE, "Last data %.1f s ago", age);
    }
    ImGui::Spacing();
    static const char *ids[16] = {"Motion",        "Session",       "LapData",     "Event",
                                  "Participants", "CarSetups",     "CarTelemetry", "CarStatus",
                                  "FinalClassification", "LobbyInfo", "CarDamage",  "SessionHistory",
                                  "TyreSets",    "MotionEx",  "TimeTrial", "LapPositions"};
    if (ImGui::BeginTable("pk", 3, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("Packet", ImGuiTableColumnFlags_WidthStretch, 3);
        ImGui::TableSetupColumn("Rate", ImGuiTableColumnFlags_WidthStretch, 1);
        ImGui::TableSetupColumn("Total", ImGuiTableColumnFlags_WidthStretch, 1);
        ImGui::PushStyleColor(ImGuiCol_Text, v4(C_DIM));
        ImGui::TableHeadersRow();
        ImGui::PopStyleColor();
        for (int i = 0; i < 16; i++) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            textCol(st.perId[i] ? C_TEXT : C_DIM, "%s", ids[i]);
            ImGui::TableNextColumn();
            withFont(F->num, [&] { textCol(a.perIdRate(i) > 0 ? C_GREEN : C_DIM, "%.0f Hz", a.perIdRate(i)); });
            ImGui::TableNextColumn();
            withFont(F->num, [&] { textCol(C_DIM, "%llu", (unsigned long long)st.perId[i]); });
        }
        ImGui::EndTable();
    }
    endCard();

    ImGui::SameLine(0, gap);
    beginScrollCard("rec", ImVec2(0, H), "RECORDINGS");
    ImGui::TextWrapped("Recording starts and stops with the button at the top right of Live Dash. When the game pauses or sends nothing, recording pauses too: no gaps in the file.");
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    textCol(C_DIM, "Saved to");
    ImGui::TextWrapped("%s", a.recordingsDir().c_str());
    if (ImGui::Button("Open Folder")) openFolder(a.recordingsDir());
    ImGui::SameLine();
    if (ImGui::Button("Recording Library")) g_page = REPLAY;
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    textCol(C_DIM, "GAME SETTINGS");
    ImGui::BulletText("Settings > Telemetry Settings > UDP Telemetry: On");
    ImGui::BulletText("UDP Broadcast Mode: Off; IP Address: 127.0.0.1");
    ImGui::BulletText("UDP Port: %u; Send Rate: 60 Hz; Format: 2025", a.cfg.port);
    endCard();
}

// ---- replay ----------------------------------------------------------------------------------------------------------
void pageReplay(app::App &a)
{
    static bool scanned = false;
    if (!scanned) { a.scanRecordings(); scanned = true; }
    const float W = ImGui::GetContentRegionAvail().x;

    beginCard("player", ImVec2(W, 300), "NOW PLAYING");
    if (a.source() == Source::Replay && a.replay.isOpen()) {
        withFont(F->title, [&] { ImGui::TextUnformatted(fs::path(a.replay.path()).filename().string().c_str()); });
        ImGui::SameLine();
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 6);
        textCol(C_DIM, "%llu packets   %zu laps", (unsigned long long)a.replay.packets(), a.replay.laps().size());
        transportControls(a, true);
        // the detail range follows playback, but holds still while a bar is being dragged
        static std::pair<uint64_t, uint64_t> held;
        static int heldLaps = 0;
        if (!g_dragSeek || heldLaps != g_detailLaps) held = detailWindow(a.replay, g_detailLaps);
        heldLaps = g_detailLaps;
        const auto win = held;
        ImGui::Spacing();
        textCol(C_DIM, "All");
        seekBar(a, "overview", 34, 0, a.replay.duration(), false, &win, nullptr);
        textCol(C_DIM, "Last %d laps", g_detailLaps);
        ImGui::SameLine();
        for (int n : {3, 4, 5, 8}) {
            ImGui::SameLine(0, 4);
            char b[8];
            std::snprintf(b, sizeof b, "%d", n);
            if (n == g_detailLaps) ImGui::PushStyleColor(ImGuiCol_Button, v4(alpha(C_ORANGE, 0.5f)));
            if (ImGui::SmallButton(b)) g_detailLaps = n;
            if (n == g_detailLaps) ImGui::PopStyleColor();
        }
        const Snapshot snap = a.race.snapshot();
        seekBar(a, "detail", 50, win.first, win.second, true, nullptr, &snap);
    } else {
        ImGui::TextWrapped("No recording open. Choose one from the library below, or browse for a .f1tr file elsewhere.");
        ImGui::Spacing();
        if (accentButton("Browse Files...", ImVec2(130, 34), C_ORANGE)) {
            std::string p = pickRecording(a.recordingsDir());
            if (!p.empty()) a.useReplay(p);
        }
    }
    endCard();
    ImGui::Dummy(ImVec2(0, 4));

    beginScrollCard("lib", ImVec2(W, ImGui::GetContentRegionAvail().y), "LIBRARY");
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 280);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 4);
    if (ImGui::SmallButton("Refresh")) a.scanRecordings();
    ImGui::SameLine();
    if (ImGui::SmallButton("Open Folder")) openFolder(a.recordingsDir());
    ImGui::SameLine();
    if (ImGui::SmallButton("Browse other files...")) {
        std::string p = pickRecording(a.recordingsDir());
        if (!p.empty()) a.useReplay(p);
    }
    // filter by session type
    static int filter = -1; // -1 all, else SessionGroup
    {
        static const SessionGroup groups[] = {SessionGroup::Practice, SessionGroup::Qualifying, SessionGroup::SprintShootout,
                                              SessionGroup::Race, SessionGroup::Sprint, SessionGroup::TimeTrial};
        auto chipBtn = [&](const char *label, int value) {
            const bool sel = filter == value;
            if (sel) ImGui::PushStyleColor(ImGuiCol_Button, v4(alpha(C_ORANGE, 0.5f)));
            if (ImGui::SmallButton(label)) filter = value;
            if (sel) ImGui::PopStyleColor();
            ImGui::SameLine(0, 4);
        };
        chipBtn("All", -1);
        for (auto g : groups) chipBtn(sessionGroupName(g), int(g));
        ImGui::NewLine();
    }
    if (a.recordings.empty()) {
        ImGui::Spacing();
        textCol(C_DIM, "The recordings folder is empty. Press Record at the top right of Live Dash (the simulation can be recorded too) and it shows up here.");
    }
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(8, 6));
    if (!a.recordings.empty() &&
        ImGui::BeginTable("files", 8, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerH)) {
        ImGui::TableSetupColumn("File", ImGuiTableColumnFlags_WidthStretch, 3);
        ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthStretch, 1.3f);
        ImGui::TableSetupColumn("TRACK", ImGuiTableColumnFlags_WidthStretch, 1.5f);
        ImGui::TableSetupColumn("Recorded", ImGuiTableColumnFlags_WidthStretch, 2);
        ImGui::TableSetupColumn("Length", ImGuiTableColumnFlags_WidthStretch, 1);
        ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthStretch, 1);
        ImGui::TableSetupColumn("Packets", ImGuiTableColumnFlags_WidthStretch, 1);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 130);
        ImGui::PushStyleColor(ImGuiCol_Text, v4(C_DIM));
        ImGui::TableHeadersRow();
        ImGui::PopStyleColor();
        for (const auto &r : a.recordings) {
            if (filter >= 0 && int(sessionGroup(r.info.sessionType)) != filter) continue;
            ImGui::PushID(r.path.c_str());
            ImGui::TableNextRow();
            const bool playing = a.source() == Source::Replay && a.replay.path() == r.path;
            const bool rec = r.path == a.recordingPath();
            if (playing) ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, alpha(C_ORANGE, 0.15f));
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(r.name.c_str());
            if (rec) { ImGui::SameLine(); textCol(C_RED, "RECORDING"); }
            ImGui::TableNextColumn();
            {
                const auto g = sessionGroup(r.info.sessionType);
                const ImU32 gc = g == SessionGroup::Race ? C_RED : g == SessionGroup::Sprint ? C_ORANGE
                               : g == SessionGroup::Qualifying || g == SessionGroup::SprintShootout ? C_PURPLE
                               : g == SessionGroup::Practice ? C_GREEN : C_DIM;
                textCol(gc, "%s", r.info.sessionType >= 0 ? sessionTypeName(r.info.sessionType) : "-");
            }
            ImGui::TableNextColumn();
            {
                const tracks::Shape *sh = r.info.trackId >= 0 ? tracks::forGame(r.info.trackId) : nullptr;
                textCol(C_DIM, "%s", sh ? sh->name.c_str() : r.info.trackId >= 0 ? tracks::asciiName(r.info.trackId).c_str() : "-");
            }
            ImGui::TableNextColumn();
            char tb[32];
            std::tm tm{};
            localtime_s(&tm, &r.mtime);
            std::strftime(tb, sizeof tb, "%Y-%m-%d %H:%M", &tm);
            textCol(C_DIM, "%s", tb);
            ImGui::TableNextColumn();
            withFont(F->num, [&] { ImGui::TextUnformatted(r.info.ok ? app::formatDuration(double(r.info.durationNs) / 1e9).c_str() : "Invalid"); });
            ImGui::TableNextColumn();
            textCol(C_DIM, "%s", app::formatBytes(r.info.bytes).c_str());
            ImGui::TableNextColumn();
            withFont(F->num, [&] { textCol(C_DIM, "%llu", (unsigned long long)r.info.packets); });
            ImGui::TableNextColumn();
            ImGui::BeginDisabled(rec || !r.info.ok);
            if (ImGui::SmallButton(playing ? "Replay Again" : "Replay")) a.useReplay(r.path);
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::BeginDisabled(rec);
            if (ImGui::SmallButton("Delete")) { g_deletePath = r.path; ImGui::OpenPopup("del"); }
            ImGui::EndDisabled();
            if (ImGui::BeginPopupModal("del", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                ImGui::Text("Delete %s? This cannot be undone.", fs::path(g_deletePath).filename().string().c_str());
                ImGui::Spacing();
                if (accentButton("Delete", ImVec2(100, 0))) {
                    if (a.source() == Source::Replay && a.replay.path() == g_deletePath) a.useLive();
                    std::error_code ec;
                    fs::remove(g_deletePath, ec);
                    a.scanRecordings();
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel", ImVec2(100, 0))) ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ImGui::PopStyleVar();
    endCard();
}

// ---- forwarding ------------------------------------------------------------------------------------------------------
void pageForward(app::App &a)
{
    const float W = ImGui::GetContentRegionAvail().x;
    const auto ts = a.hub.targets();

    // flow diagram
    beginCard("flow", ImVec2(W, 170), "DATA FLOW");
    {
        ImDrawList *dl = ImGui::GetWindowDrawList();
        ImVec2 o = ImGui::GetCursorScreenPos();
        const float h = 100, bw = 190;
        auto box = [&](float x, float y, float w, float bh, const char *t1, const std::string &t2, ImU32 col) {
            dl->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + bh), C_PANEL2, 6);
            dl->AddRect(ImVec2(x, y), ImVec2(x + w, y + bh), col, 6, 0, 1.5f);
            dl->AddText(ImVec2(x + 12, y + 8), C_TEXT, t1);
            dl->AddText(ImVec2(x + 12, y + bh - 26), C_DIM, t2.c_str());
        };
        auto arrow = [&](ImVec2 a0, ImVec2 a1, ImU32 col) {
            dl->AddLine(a0, a1, col, 2);
            dl->AddTriangleFilled(a1, ImVec2(a1.x - 9, a1.y - 5), ImVec2(a1.x - 9, a1.y + 5), col);
        };
        const float midY = o.y + h / 2;
        box(o.x, midY - 32, bw, 64, "F1 25", "UDP > 127.0.0.1:" + std::to_string(a.cfg.port), C_DIM);
        const float hx = o.x + bw + 70;
        box(hx, midY - 40, bw + 20, 80, "f1-race hub", "Dash · recording · replay", C_ACCENT);
        arrow(ImVec2(o.x + bw + 4, midY), ImVec2(hx - 4, midY), alpha(C_TEXT, 0.6f));
        const float tx = hx + bw + 20 + 80;
        const int n = std::max(1, int(ts.size()));
        const float tw = std::min(260.f, ImGui::GetContentRegionAvail().x - (tx - o.x));
        for (int i = 0; i < int(ts.size()) && i < 4; i++) {
            float y = o.y + (n == 1 ? h / 2 - 16 : float(i) * (h - 32) / float(std::min(n, 4) - 1));
            const auto &t = ts[i];
            ImU32 col = !t.target.enabled ? C_DIM : !t.resolved ? C_RED : a.targetRate(i) > 0 ? C_GREEN : C_TEXT;
            arrow(ImVec2(hx + bw + 24, midY), ImVec2(tx - 4, y + 16), alpha(col, 0.6f));
            dl->AddRectFilled(ImVec2(tx, y), ImVec2(tx + tw, y + 32), C_PANEL2, 5);
            char b[96];
            std::snprintf(b, sizeof b, "%s:%u  %s", t.target.host.c_str(), t.target.port, t.target.note.c_str());
            dl->PushClipRect(ImVec2(tx, y), ImVec2(tx + tw - 6, y + 32), true);
            dl->AddText(ImVec2(tx + 10, y + 7), col, b);
            dl->PopClipRect();
        }
        if (ts.empty()) dl->AddText(ImVec2(tx, midY - 8), C_DIM, "No targets");
        ImGui::Dummy(ImVec2(0, h + 10));
    }
    endCard();
    ImGui::Dummy(ImVec2(0, 4));

    beginScrollCard("targets", ImVec2(W, ImGui::GetContentRegionAvail().y), "TARGETS (changes apply at once and are saved)");
    // listen port
    {
        static int port = -1;
        if (port < 0) port = a.cfg.port;
        textCol(C_DIM, "Listen port (the game sends here)");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(110);
        ImGui::InputInt("##port", &port, 0);
        port = std::clamp(port, 1, 65535);
        ImGui::SameLine();
        ImGui::BeginDisabled(port == a.cfg.port);
        if (ImGui::Button("Apply")) {
            a.cfg.port = uint16_t(port);
            a.dirty = true;
            a.relisten();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        textCol(C_DIM, a.source() == app::Source::Live ? (a.hub.listening() ? "Listening" : "Not listening (port in use?)")
                                                       : "(not on live telemetry: takes effect when you switch)");
    }
    ImGui::Spacing();
    bool changed = false;
    int remove = -1;
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(8, 5));
    if (ImGui::BeginTable("t", 8, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_BordersInnerH)) {
        ImGui::TableSetupColumn("On", 0, 40);
        ImGui::TableSetupColumn("Address", 0, 150);
        ImGui::TableSetupColumn("Port", 0, 90);
        ImGui::TableSetupColumn("Note", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Rate", 0, 80);
        ImGui::TableSetupColumn("Sent", 0, 100);
        ImGui::TableSetupColumn("Errors", 0, 60);
        ImGui::TableSetupColumn("", 0, 50);
        ImGui::PushStyleColor(ImGuiCol_Text, v4(C_DIM));
        ImGui::TableHeadersRow();
        ImGui::PopStyleColor();
        for (size_t i = 0; i < a.cfg.targets.size(); i++) {
            auto &t = a.cfg.targets[i];
            ImGui::PushID(int(i));
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            changed |= ImGui::Checkbox("##en", &t.enabled);
            ImGui::TableNextColumn();
            char host[64];
            std::snprintf(host, sizeof host, "%s", t.host.c_str());
            ImGui::SetNextItemWidth(-1);
            if (ImGui::InputText("##host", host, sizeof host)) t.host = host;
            changed |= ImGui::IsItemDeactivatedAfterEdit();
            ImGui::TableNextColumn();
            int p = t.port;
            ImGui::SetNextItemWidth(-1);
            if (ImGui::InputInt("##p", &p, 0)) t.port = uint16_t(std::clamp(p, 1, 65535));
            changed |= ImGui::IsItemDeactivatedAfterEdit();
            ImGui::TableNextColumn();
            char note[128];
            std::snprintf(note, sizeof note, "%s", t.note.c_str());
            ImGui::SetNextItemWidth(-1);
            if (ImGui::InputText("##note", note, sizeof note)) t.note = note;
            changed |= ImGui::IsItemDeactivatedAfterEdit();
            ImGui::TableNextColumn();
            const bool bad = i < ts.size() && !ts[i].resolved;
            withFont(F->num, [&] {
                if (bad) textCol(C_RED, "Bad address");
                else textCol(a.targetRate(i) > 0 ? C_GREEN : C_DIM, "%.0f/s", a.targetRate(i));
            });
            ImGui::TableNextColumn();
            withFont(F->num, [&] { textCol(C_DIM, "%llu", i < ts.size() ? (unsigned long long)ts[i].sent : 0ull); });
            ImGui::TableNextColumn();
            withFont(F->num, [&] {
                const auto e = i < ts.size() ? ts[i].errors : 0;
                textCol(e ? C_RED : C_DIM, "%llu", (unsigned long long)e);
            });
            ImGui::TableNextColumn();
            if (ImGui::SmallButton("Delete")) remove = int(i);
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ImGui::PopStyleVar();
    if (remove >= 0) {
        a.cfg.targets.erase(a.cfg.targets.begin() + remove);
        changed = true;
    }
    ImGui::Spacing();
    auto add = [&](uint16_t port, const char *note) {
        a.cfg.targets.push_back({"127.0.0.1", port, true, note});
        changed = true;
    };
    if (ImGui::Button("+ Add Target")) add(20778, "");
    ImGui::SameLine();
    textCol(C_DIM, "Common:");
    ImGui::SameLine();
    if (ImGui::SmallButton("20777 Wheel Dash")) add(20777, "Tools fixed to 20777, such as Wheel Dash");
    ImGui::SameLine();
    if (ImGui::SmallButton("20778 SimHub")) add(20778, "SimHub");
    ImGui::SameLine();
    if (ImGui::SmallButton("20779 other programs")) add(20779, "");
    if (changed) {
        a.applyTargets();
        a.dirty = true;
    }
    ImGui::Spacing();
    textCol(C_DIM, "Every target gets exactly the raw packets the game sends; replay and simulation data is forwarded too, to test other tools without the game.");
    textCol(C_DIM, "Use another PC's LAN IP to forward to that PC.");
    endCard();
}

// ---- HMD panels --------------------------------------------------------------------------------------------------------
void pageHmd(app::App &a, VrEnv &env)
{
    auto &cfg = a.cfg;
    const float H = ImGui::GetContentRegionAvail().y;
    beginScrollCard("vrset", ImVec2(440, H), nullptr);
    ImGui::SeparatorText("STEAMVR / HEADSET");
    ImGui::TextWrapped("%s", env.status->c_str());
    if (!env.vr->running()) {
        if (ImGui::Button("Launch SteamVR")) env.launch();
        ImGui::SameLine();
        if (ImGui::Button("Reconnect SteamVR")) env.reconnect();
        ImGui::SameLine();
        textCol(C_DIM, "The program never starts SteamVR by itself; it connects within 5 s once SteamVR runs");
    }
    const auto &hmd = *env.hmd;
    if (env.vr->hmd().valid) ImGui::Text("Model: %s", env.vr->hmd().model.c_str());
    ImGui::Text("Per eye %u x %u   about %.0f px/deg", hmd.renderW, hmd.renderH, hmd.ppd());
    ImGui::Text("FOV %.0f° x %.0f°   overlap %.0f°", hmd.right - hmd.left, hmd.up - hmd.down, hmd.binoRight - hmd.binoLeft);
    if (hmd.cantDeg > 0.5f) ImGui::Text("Display cant %.0f°", hmd.cantDeg);
    ImGui::Text("Panel texture %u x %u (%.2fx)", env.texSize, env.texSize, env.texScale);
    if (!env.vr->hmd().valid && !cfg.hmd.fovH) textCol(C_DIM, "No headset found; using defaults. Set them by hand under hmd.* in f1-race.ini");

    ImGui::SeparatorText("OPACITY");
    float bgPct = cfg.bgOpacity * 100.f, fgPct = cfg.textOpacity * 100.f;
    if (ImGui::SliderFloat("Background opacity", &bgPct, 0.f, 100.f, "%.0f%%")) { cfg.bgOpacity = bgPct / 100.f; a.dirty = true; }
    if (ImGui::SliderFloat("Content opacity", &fgPct, 10.f, 100.f, "%.0f%%")) { cfg.textOpacity = fgPct / 100.f; a.dirty = true; }
    textCol(C_DIM, "Background = panel fill; content = text, cells, icons");

    ImGui::SeparatorText("STANDINGS GAP");
    if (ImGui::RadioButton("TO ME", &cfg.gapMode, 0)) a.dirty = true;
    ImGui::SameLine();
    if (ImGui::RadioButton("INTERVAL", &cfg.gapMode, 1)) a.dirty = true;
    ImGui::SameLine();
    if (ImGui::RadioButton("To leader", &cfg.gapMode, 2)) a.dirty = true;

    const char *titles[layout::PANELS] = {"STANDINGS (TOP LEFT)", "SECTORS (TOP RIGHT)", "PEDALS (BOTTOM EDGE)", "CAR (UNDER STANDINGS)", "STATUS & FAULTS (UNDER SECTORS)", "WEATHER (TOP)"};
    for (int i = 0; i < layout::PANELS; i++) {
        ImGui::PushID(i);
        ImGui::SeparatorText(titles[i]);
        const auto &fit = env.fits[i];
        textCol(C_DIM, "Base: %s, centre %s%.1f° %s%.1f°, width %.0f°", layout::BASE[i].below >= 0 ? "under the panel above" : "on the usable view's edge",
                fit.azDeg < 0 ? "L" : "R", std::fabs(fit.azDeg), fit.elDeg < 0 ? "D" : "U", std::fabs(fit.elDeg),
                fit.widthDeg);
        auto &o = cfg.offset[i];
        bool ch = false;
        ch |= ImGui::Checkbox("Show", &o.visible);
        ImGui::SameLine();
        if (ImGui::SmallButton("Reset Position")) {
            const bool v = o.visible;
            o = layout::defaultOffset(i);
            o.visible = v;
            ch = true;
        }
        int mode = o.cockpit ? 1 : 0;
        bool modeCh = ImGui::RadioButton("Follow head", &mode, 0);
        ImGui::SameLine();
        modeCh |= ImGui::RadioButton("Fixed in cockpit", &mode, 1);
        if (modeCh) {
            if (mode == 1 && !o.cockpit) { // keep it where it is seen now, looking straight ahead
                const auto p = layout::place(fit, o);
                o.cx = p.x;
                o.cy = p.y;
                o.cz = p.z;
            }
            o.cockpit = mode == 1;
            ch = true;
        }
        if (o.cockpit) {
            ch |= ImGui::SliderFloat("Left / right", &o.cx, -0.8f, 0.8f, "%+.2f m");
            ch |= ImGui::SliderFloat("Up / down", &o.cy, -1.0f, 0.5f, "%+.2f m");
            ch |= ImGui::SliderFloat("Forward / back", &o.cz, -1.5f, -0.2f, "%+.2f m");
            textCol(C_DIM, "Cockpit coordinates: origin = head position at SteamVR's seated reset; negative = forward");
        } else {
            ch |= ImGui::SliderFloat("Move left / right", &o.daz, -60.f, 60.f, "%+.1f°");
            ch |= ImGui::SliderFloat("Move up / down", &o.del, -60.f, 60.f, "%+.1f°");
            ch |= ImGui::SliderFloat("Distance offset", &o.dz, -0.5f, 0.5f, "%+.2f m");
        }
        ch |= ImGui::SliderFloat("Yaw offset", &o.dyaw, -45.f, 45.f, "%+.0f°");
        ch |= ImGui::SliderFloat("Pitch offset", &o.dpitch, -45.f, 45.f, "%+.0f°");
        ch |= ImGui::SliderFloat("Scale", &o.scale, 0.5f, 2.0f, "%.2fx");
        if (ch) { // panels stacked under this one move with it
            layout::fitAll(*env.layoutHmd, cfg.offset, env.fits);
            for (int j = 0; j < layout::PANELS; j++) env.vr->setPlacement(j, layout::place(env.fits[j], cfg.offset[j]));
            a.dirty = true;
        }
        ImGui::PopID();
    }
    ImGui::Spacing();
    textCol(C_DIM, "Head-locked panels: left / right and up / down move by view angle; the panel always faces the eyes");
    if (ImGui::Button("Save")) {
        a.save();
        LOG("settings saved by button");
    }
    ImGui::SameLine();
    if (a.dirty) textCol(C_DIM, "Changed: saving within 1 s");
    else if (a.savedAt) {
        char t[32];
        std::strftime(t, sizeof t, "%H:%M:%S", std::localtime(&a.savedAt));
        textCol(C_DIM, "Saved to f1-race.ini (%s)", t);
    } else textCol(C_DIM, "Saved in f1-race.ini");
    endCard();

    ImGui::SameLine(0, 12);
    beginCard("vrprev", ImVec2(0, H), "PANEL TEXTURE PREVIEW (each panel is its own overlay in the headset)");
    ImVec2 avail = ImGui::GetContentRegionAvail();
    float side = std::min(avail.x, avail.y);
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddRectFilled(p0, ImVec2(p0.x + side, p0.y + side), IM_COL32(40, 46, 54, 255), 4);
    ImGui::Image((ImTextureID)(intptr_t)env.panelTexture, ImVec2(side, side));
    endCard();
}

// ---- settings --------------------------------------------------------------------------------------------------------
// ---- game launcher ---------------------------------------------------------------------------------------------
// Online: Steam starts the stock game. Offline: Swofty's command line applies a profile's mods for the session.
launcher::Launcher g_launch;

// Wheel buttons: each plugin (the Lytton Dash mod, later the program itself) has its own controller; pick it, press
// Bind, press the button (a rotary knob's click in each direction works too). Saved to wheel_buttons.ini at once;
// the mod picks it up in the game within a second.
wheel::Reader g_wheel;
void wheelButtons()
{
    static bool loaded = false;
    static wheel::Bindings all;
    static std::string bindPlugin, bindAction; // the action waiting for a press
    auto watchAll = [] {
        std::set<std::string> g;
        for (const auto &[id, b] : all)
            if (!b.device.empty()) g.insert(b.device);
        g_wheel.watch(g);
    };
    if (!loaded) {
        all = wheel::load();
        g_wheel.start();
        watchAll();
        loaded = true;
    }
    ImGui::Spacing();
    ImGui::SeparatorText("WHEEL BUTTONS");
    const auto devs = g_wheel.devices();
    if (ImGui::SmallButton("Rescan controllers")) g_wheel.rescan();
    ImGui::SameLine();
    textCol(C_DIM, "%d connected", int(devs.size()));

    if (!bindAction.empty()) {
        const int got = g_wheel.captured();
        if (got > 0) {
            wheel::Binding &b = all[bindPlugin];
            for (auto &[id, n] : b.button)
                if (id != bindAction && n == got) n = 0; // one button, one action (within a plugin)
            b.button[bindAction] = got;
            wheel::save(all);
            bindAction.clear();
        } else if (!g_wheel.capturing()) bindAction.clear();
    }

    for (const auto &plug : wheel::plugins()) {
        ImGui::PushID(plug.id);
        wheel::Binding &b = all[plug.id];
        const wheel::Device *cur = nullptr;
        for (const auto &d : devs)
            if (d.guid == b.device) cur = &d;
        // not plugged in under its saved instance GUID: the same model under a new one counts as the same controller
        if (!cur && !b.product.empty())
            for (const auto &d : devs)
                if (d.product == b.product) {
                    cur = &d;
                    b.device = d.guid;
                    wheel::save(all);
                    watchAll();
                    break;
                }
        ImGui::Spacing();
        textCol(C_TEXT, "%s", plug.title);
        textCol(C_DIM, "Controller");
        ImGui::SameLine(230);
        ImGui::SetNextItemWidth(std::max(160.f, ImGui::GetContentRegionAvail().x));
        const std::string label = cur ? cur->name : !b.deviceName.empty() ? b.deviceName + " (not connected)" : "Choose a controller";
        if (ImGui::BeginCombo("##dev", label.c_str())) {
            for (const auto &d : devs) {
                char l[200];
                std::snprintf(l, sizeof l, "%s  (%d buttons)##%s", d.name.c_str(), d.buttons, d.guid.c_str());
                if (ImGui::Selectable(l, cur && d.guid == cur->guid)) {
                    if (d.product != b.product) b.button.clear(); // another model: its buttons mean something else
                    b.device = d.guid;
                    b.product = d.product;
                    b.deviceName = d.name;
                    wheel::save(all);
                    watchAll();
                    if (bindPlugin == plug.id) { bindAction.clear(); g_wheel.cancelCapture(); }
                }
            }
            if (devs.empty()) textCol(C_DIM, "No game controllers found");
            ImGui::EndCombo();
        }
        const auto held = cur ? g_wheel.pressed(cur->guid) : std::vector<int>{};
        ImGui::BeginDisabled(!cur);
        for (const auto &act : wheel::actions()) {
            if (std::strcmp(act.plugin, plug.id) != 0) continue;
            ImGui::PushID(act.id);
            const bool waiting = bindPlugin == plug.id && bindAction == act.id;
            const int n = b.get(act.id);
            textCol(C_DIM, "%s", act.label);
            ImGui::SameLine(230);
            const bool on = n > 0 && std::find(held.begin(), held.end(), n) != held.end();
            if (waiting) textCol(C_YELLOW, "Press a button...");
            else if (n > 0) textCol(on ? C_GREEN : C_TEXT, "Button %d", n);
            else textCol(C_DIM, "not bound");
            ImGui::SameLine(400);
            if (waiting) {
                if (ImGui::SmallButton("Cancel")) { bindAction.clear(); g_wheel.cancelCapture(); }
            } else if (ImGui::SmallButton("Bind")) {
                bindPlugin = plug.id;
                bindAction = act.id;
                g_wheel.capture(cur->guid);
            }
            if (n > 0 && !waiting) {
                ImGui::SameLine();
                if (ImGui::SmallButton("Clear")) { b.button.erase(act.id); wheel::save(all); }
            }
            ImGui::PopID();
        }
        ImGui::EndDisabled();
        if (cur) {
            std::string h;
            for (int x : held) h += (h.empty() ? "" : ", ") + std::to_string(x);
            textCol(C_DIM, "Held now: %s", h.empty() ? "-" : h.c_str());
        }
        ImGui::PopID();
    }
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Text, v4(C_DIM));
    ImGui::TextWrapped("Each plugin reads its own controller (a knob turned left and right can be two actions). The game "
                       "still gets the button too: pick one it does not use, or one you want for both. The Lytton Dash "
                       "mod runs only in offline sessions.");
    ImGui::PopStyleColor();
}

void pageGame(app::App &a)
{
    static bool first = true;
    if (first) {
        g_launch.refresh(a.cfg.launchProfile);
        first = false;
    }
    const launcher::Status st = g_launch.status();
    const bool busy = g_launch.busy();
    const std::string profile = !a.cfg.launchProfile.empty() ? a.cfg.launchProfile : st.activeProfile;
    const float W = ImGui::GetContentRegionAvail().x, H = ImGui::GetContentRegionAvail().y, gap = 12;
    const float leftW = std::floor((W - gap) * 0.58f), rightW = W - gap - leftW;
    const bool mods = a.cfg.launchMods;

    // two-way choice as a pair of buttons, the chosen one filled
    auto choice = [&](const char *id, bool &value, const char *offLabel, const char *onLabel, ImU32 onCol) {
        ImGui::PushID(id);
        const float bw = 250, y = ImGui::GetCursorPosY();
        for (int k = 0; k < 2; k++) {
            if (k) {
                ImGui::SameLine(0, 6);
                ImGui::SetCursorPosY(y); // the label beside the row sits lower: keep both buttons level
            }
            const bool sel = value == (k == 1);
            ImGui::PushStyleColor(ImGuiCol_Button, v4(sel ? (k ? onCol : C_PANEL2) : C_PANEL));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, v4(sel ? (k ? onCol : C_PANEL2) : alpha(C_PANEL2, 0.7f)));
            ImGui::PushStyleColor(ImGuiCol_Text, v4(sel ? C_TEXT : C_DIM));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, sel ? 0.f : 1.f);
            if (ImGui::Button(k ? onLabel : offLabel, ImVec2(bw, 40)) && value != (k == 1)) {
                value = k == 1;
                a.dirty = true;
            }
            ImGui::PopStyleVar();
            ImGui::PopStyleColor(3);
        }
        ImGui::PopID();
    };
    auto label = [](const char *t) { // level with the 40 px choice buttons beside it
        const float y = ImGui::GetCursorPosY();
        ImGui::SetCursorPosY(y + (40 - ImGui::GetTextLineHeight()) / 2);
        textCol(C_DIM, "%s", t);
        ImGui::SameLine(120);
        ImGui::SetCursorPosY(y);
    };

    // ---- left: what to start, and the button ----
    beginCard("launch", ImVec2(leftW, H), "LAUNCH OPTIONS");
    label("Display");
    choice("vr", a.cfg.launchVr, "Monitor", "VR headset (SteamVR)", C_BLUE);
    ImGui::Spacing();
    label("Mods");
    choice("mods", a.cfg.launchMods, "No mods  (online OK)", "Load mods  (offline)", C_ACCENT);
    ImGui::Spacing();

    int enabledCount = 0;
    if (mods) {
        if (!st.found) {
            textCol(C_YELLOW, "Swofty Launcher not found: install it from f1.swofty.net (it loads the mods), then Refresh.");
        } else {
            label("Profile");
            ImGui::SetNextItemWidth(300);
            if (ImGui::BeginCombo("##profile", profile.c_str())) {
                for (const auto &pr : st.profiles) {
                    const std::string l = pr + (pr == st.activeProfile ? "  (Swofty's current)" : "");
                    if (ImGui::Selectable(l.c_str(), pr == profile)) {
                        a.cfg.launchProfile = pr;
                        a.dirty = true;
                        g_launch.refresh(pr);
                    }
                }
                ImGui::EndCombo();
            }
            ImGui::Spacing();
            textCol(C_DIM, "Which mods to load");
            if (st.addonsProfile != profile) textCol(C_DIM, "Loading...");
            else if (st.addons.empty()) textCol(C_DIM, "This profile has no addons");
            else if (ImGui::BeginTable("addons", 3, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg)) {
                ImGui::TableSetupColumn("On", ImGuiTableColumnFlags_WidthFixed, 40);
                ImGui::TableSetupColumn("Addon", ImGuiTableColumnFlags_WidthStretch, 3);
                ImGui::TableSetupColumn("Version", ImGuiTableColumnFlags_WidthStretch, 1);
                for (const auto &ad : st.addons) {
                    if (ad.shader()) continue; // chosen below, as the profile's shader config
                    if (ad.enabled() && !ad.builtIn()) enabledCount++;
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    bool on = ad.enabled();
                    ImGui::BeginDisabled(ad.builtIn() || busy || profile == "vanilla");
                    ImGui::PushID(ad.id.c_str());
                    if (ImGui::Checkbox("##on", &on)) g_launch.setAddon(profile, ad.id, on);
                    ImGui::PopID();
                    ImGui::EndDisabled();
                    ImGui::TableNextColumn();
                    textCol(C_TEXT, "%s", ad.name.c_str());
                    ImGui::SameLine();
                    textCol(C_DIM, "%s", ad.id.c_str());
                    ImGui::TableNextColumn();
                    textCol(C_DIM, "%s%s", ad.version.c_str(), ad.builtIn() ? "  (always on)" : "");
                }
                ImGui::EndTable();
            }
            // the profile's shader config (colour grading and post effects), one at a time
            if (st.addonsProfile == profile) {
                ImGui::Spacing();
                label("Shaders");
                std::string cur = "Off";
                for (const auto &sh : st.shaders)
                    if (sh.id == st.activeShader) cur = sh.name;
                ImGui::SetNextItemWidth(300);
                ImGui::BeginDisabled(busy || profile == "vanilla");
                if (ImGui::BeginCombo("##shader", cur.c_str())) {
                    if (ImGui::Selectable("Off", st.activeShader == "off" || st.activeShader.empty())) g_launch.setShader(profile, "off");
                    for (const auto &sh : st.shaders)
                        if (ImGui::Selectable(sh.name.c_str(), sh.id == st.activeShader)) g_launch.setShader(profile, sh.id);
                    ImGui::EndCombo();
                }
                ImGui::EndDisabled();
                ImGui::Dummy(ImVec2(0, 4));
            }
            ImGui::PushStyleColor(ImGuiCol_Text, v4(C_DIM));
            ImGui::TextWrapped("New addons, content mods and profiles are added in Swofty Launcher.");
            ImGui::PopStyleColor();
            if (st.checked && !(st.consent && st.extension))
                textCol(C_YELLOW, "First time: open Swofty Launcher and press Play once (accept its offline terms).");
        }
    } else {
        ImGui::PushStyleColor(ImGuiCol_Text, v4(C_DIM));
        ImGui::TextWrapped("The stock game through Steam: EA AntiCheat on, online races and two-player career work.");
        ImGui::PopStyleColor();
        if (st.found && st.checked && !st.vanilla)
            textCol(C_YELLOW, "The game files were left changed by a modded session: Restore them first (right).");
    }

    // the button, at the bottom of the card, saying what it will do
    const float btnH = 96;
    ImGui::SetCursorPosY(std::max(ImGui::GetCursorPosY() + 12, ImGui::GetWindowHeight() - btnH - 44));
    {
        char title[96], sub[160];
        std::snprintf(title, sizeof title, "PLAY  %s%s", mods ? "OFFLINE + MODS" : "ONLINE", a.cfg.launchVr ? "  ·  VR" : "");
        if (mods)
            std::snprintf(sub, sizeof sub, "Swofty, profile %s, %d mod%s on; single-player only, EA AntiCheat off for the session",
                          profile.empty() ? "-" : profile.c_str(), enabledCount, enabledCount == 1 ? "" : "s");
        else std::snprintf(sub, sizeof sub, "Steam, stock game; EA AntiCheat on, multiplayer");
        const bool can = !busy && (!mods || (st.found && !profile.empty()));
        const ImU32 col = mods ? C_ACCENT : C_GREEN;
        ImGui::BeginDisabled(!can);
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float bw = ImGui::GetContentRegionAvail().x;
        const bool hit = ImGui::InvisibleButton("play", ImVec2(bw, btnH));
        ImDrawList *dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(p, ImVec2(p.x + bw, p.y + btnH), alpha(col, !can ? 0.25f : ImGui::IsItemHovered() ? 0.95f : 0.8f), 8);
        dl->AddText(F->numMid, fsz(F->numMid), ImVec2(p.x + 22, p.y + 14), IM_COL32_WHITE, title);
        dl->AddText(ImVec2(p.x + 22, p.y + btnH - 30), alpha(IM_COL32_WHITE, 0.85f), sub);
        ImGui::EndDisabled();
        if (hit) {
            if (mods) g_launch.playOffline(profile, a.cfg.launchVr);
            else g_launch.playOnline(a.cfg.launchVr);
        }
        if (busy) textCol(C_DIM, "Working...");
        else textCol(C_TEXT, "%s", g_launch.message().c_str());
    }
    endCard();

    // ---- right: Swofty and the game files ----
    ImGui::SameLine(0, gap);
    beginScrollCard("swofty", ImVec2(rightW, H), "GAME AND SWOFTY");
    if (!st.checked) textCol(C_DIM, "Checking...");
    else {
        auto row = [](const char *k) {
            textCol(C_DIM, "%s", k);
            ImGui::SameLine(130);
        };
        if (!st.found) {
            row("Swofty");
            textCol(C_YELLOW, "not installed (only needed for mods)");
        } else {
            row("Swofty");
            textCol(C_TEXT, "%s", st.version.c_str());
            row("Game");
            textCol(C_TEXT, "build %s", st.gameBuild.c_str());
            row("Folder");
            textCol(C_DIM, "%s", st.gameDir.c_str());
            row("Game files");
            if (st.vanilla) textCol(C_GREEN, "Stock: fine for online");
            else {
                textCol(C_YELLOW, "Changed by a modded session");
                ImGui::SameLine();
                ImGui::BeginDisabled(busy || st.gameRunning);
                if (ImGui::SmallButton("Restore")) g_launch.restore();
                ImGui::EndDisabled();
            }
            row("Offline mods");
            if (st.consent && st.extension) textCol(C_GREEN, "Ready");
            else textCol(C_YELLOW, "One-time setup in Swofty Launcher");
        }
        if (st.gameRunning) textCol(C_ORANGE, "F1 25 is running");
        ImGui::Spacing();
        if (st.found && ImGui::Button("Open Swofty Launcher")) g_launch.openSwofty();
        if (st.found) ImGui::SameLine();
    }
    ImGui::BeginDisabled(busy);
    if (ImGui::Button("Refresh")) g_launch.refresh(a.cfg.launchProfile);
    ImGui::EndDisabled();
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Text, v4(C_DIM));
    ImGui::TextWrapped("Mods load only in sessions started offline through Swofty: it turns EA AntiCheat off for that "
                       "session and puts the game files back when the game exits. Telemetry and the headset panels work "
                       "in both modes.");
    ImGui::PopStyleColor();
    wheelButtons();
    endCard();
}

void pageSettings(app::App &a)
{
    beginScrollCard("set", ImVec2(std::min(760.f, ImGui::GetContentRegionAvail().x), ImGui::GetContentRegionAvail().y), nullptr);
    ImGui::SeparatorText("START-UP");
    textCol(C_DIM, "Data source at start-up");
    if (ImGui::RadioButton("Live telemetry (racing)", &a.cfg.startSource, 0)) a.dirty = true;
    ImGui::SameLine();
    if (ImGui::RadioButton("Simulation (demo)", &a.cfg.startSource, 1)) a.dirty = true;
    textCol(C_DIM, "Command line: --demo always uses the simulation, --replay <file> plays a file");

    ImGui::SeparatorText("Simulation");
    ImGui::SetNextItemWidth(260);
    if (ImGui::SliderFloat("Time scale", &a.cfg.simSpeed, 0.25f, 8.f, "%.2fx", ImGuiSliderFlags_Logarithmic)) a.dirty = true;
    if (ImGui::Button("Restart Simulation")) a.useSim();
    ImGui::SameLine();
    textCol(C_DIM, "The scale applies after a restart");

    ImGui::SeparatorText("RECORDINGS");
    ImGui::TextWrapped("%s", a.recordingsDir().c_str());
    if (ImGui::Button("Open Recordings Folder")) openFolder(a.recordingsDir());

    ImGui::SeparatorText("DRIVER");
    {
        char b[8];
        std::snprintf(b, sizeof b, "%s", a.cfg.myCode.c_str());
        ImGui::SetNextItemWidth(80);
        if (ImGui::InputText("My code", b, sizeof b, ImGuiInputTextFlags_CharsUppercase | ImGuiInputTextFlags_CharsNoBlank)) {
            a.cfg.myCode = b;
            timing::setMyCode(a.cfg.myCode);
            a.dirty = true;
        }
        ImGui::SameLine();
        textCol(C_DIM, "Your three-letter code in the standings; empty = from the first Latin word of your name: first letter + consonants (Lyton -> LTN)");
    }

    ImGui::SeparatorText("FONT");
    {
        // the fonts installed on this PC (system and per user); the program only reads them, nothing is shipped
        static std::vector<std::string> fonts; // UTF-8 paths
        if (fonts.empty()) {
            std::vector<std::filesystem::path> dirs = {"C:/Windows/Fonts"};
            if (const char *la = std::getenv("LOCALAPPDATA")) dirs.push_back(std::filesystem::path(la) / "Microsoft/Windows/Fonts");
            std::error_code ec;
            for (const auto &d : dirs)
                for (const auto &e : std::filesystem::directory_iterator(d, ec)) {
                    std::string ext = e.path().extension().string();
                    for (auto &ch : ext) ch = char(std::tolower((unsigned char)ch));
                    if (ext == ".ttf" || ext == ".otf") fonts.push_back(e.path().u8string());
                }
            std::sort(fonts.begin(), fonts.end(), [](const std::string &x, const std::string &y) {
                return std::filesystem::u8path(x).filename().u8string() < std::filesystem::u8path(y).filename().u8string();
            });
        }
        auto label = [](const std::string &p) { return p.empty() ? std::string("Default") : std::filesystem::u8path(p).filename().u8string(); };
        ImGui::SetNextItemWidth(320);
        if (ImGui::BeginCombo("Latin font", label(a.cfg.latinFont).c_str(), ImGuiComboFlags_HeightLarge)) {
            if (ImGui::Selectable("Default", a.cfg.latinFont.empty())) { a.cfg.latinFont.clear(); a.dirty = true; }
            for (const auto &f : fonts)
                if (ImGui::Selectable(label(f).c_str(), f == a.cfg.latinFont)) { a.cfg.latinFont = f; a.dirty = true; }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        if (ImGui::Checkbox("Digits and signs too", &a.cfg.latinDigits)) a.dirty = true;
        textCol(C_DIM, "Replaces the Latin letters; ticked, also digits and signs (%% ° / : etc.); characters the font lacks use the default font; desktop and headset; applies after a restart");
        textCol(C_DIM, "Read from this PC, never shipped with the program; PCs without the font use the default");
    }

    ImGui::SeparatorText("ABOUT");
    ImGui::Text("Lytton Telemetry Tool  v%s", F1RACE_VERSION);
    textCol(C_DIM, "Reads only EA's official UDP telemetry: no injection, no game files changed, fine online.");
    textCol(C_DIM, "Free software under GPL-3.0 (LICENSE in the install folder).");
    ImGui::Spacing();
    textCol(C_DIM, "CREDITS");
    ImGui::BulletText("Track N Race (NoGoat): racing lines and DRS zones of the circuits, GPL-3.0");
    ImGui::BulletText("Dear ImGui（MIT） · OpenVR（BSD） · racetrack-database / TUMFTM（LGPL-3.0） · f1-circuits（MIT）");
    textCol(C_DIM, "Full list in the install folder's licence file");
    textCol(C_DIM, "Settings file: %s", a.iniPath.c_str());
    if (ImGui::Button("Open Program Folder")) openFolder(a.dir);
    endCard();
}

} // namespace

// ---- pieces for the Qt window (apps/dash-qt): each drawn into its own ImGui canvas ----------------------------------
void bind(const Fonts &f) { F = &f; }
int page() { return g_page; }
bool mapFull() { return g_mapFull; }
void setMapFull(bool on) { g_mapFull = on; }
bool transportShown(app::App &a)
{
    return a.source() == Source::Replay && a.replay.isOpen() && g_page != REPLAY && g_page != COMPARE;
}
void drawSidebar(app::App &a) { sidebar(a); }
void drawTopBar(app::App &a) { topBar(a); }
void drawTransport(app::App &a) { bottomTransport(a); }
void drawPage(app::App &a, VrEnv &vr, int p)
{
    const Snapshot snap = a.race.snapshot();
    switch (p) {
    case DASH: pageDashboard(a, snap); break;
    case SETUP: pageSetup(a, snap); break;
    case COMPARE: pageCompare(a); break;
    case CAPTURE: pageCapture(a); break;
    case REPLAY: pageReplay(a); break;
    case FORWARD: pageForward(a); break;
    case HMD: pageHmd(a, vr); break;
    case SETTINGS: pageSettings(a); break;
    case GAME: pageGame(a); break;
    }
}
bool drawEmpty(app::App &a, const Snapshot &s) { return emptyState(a, s); }
void drawStats(const Snapshot &s, float w)
{
    const float gap = 12, cw = (w - 7 * gap) / 8; // six numbers and the forecast (two cards wide)
    const Car *me = s.playerIdx < f1tel::MAX_CARS ? &s.cars[s.playerIdx] : nullptr;
    char pos[32] = "-", lap[32] = "-";
    if (me) {
        std::snprintf(pos, sizeof pos, "P%d", me->position);
        std::snprintf(lap, sizeof lap, "%d", me->lapNum);
    }
    char of[32];
    std::snprintf(of, sizeof of, "/ %d", int(s.order().size()));
    statCard("c1", cw, "POSITION", pos, C_TEXT, of);
    ImGui::SameLine(0, gap);
    statCard("c2", cw, "LAP", lap);
    ImGui::SameLine(0, gap);
    statCard("c3", cw, me && me->currentLapInvalid ? "CURRENT (INVALID)" : "CURRENT", me ? formatLap(me->currentLapMs) : "-",
             me && me->currentLapInvalid ? C_RED : C_TEXT);
    ImGui::SameLine(0, gap);
    {
        Mark m = me && !me->laps.empty() ? markLap(s, s.playerIdx, me->lastLapMs, me->laps.back().lapValid) : Mark::None;
        statCard("c4", cw, "LAST LAP", me ? formatLap(me->lastLapMs) : "-", m == Mark::None ? C_TEXT : markColour(m));
    }
    ImGui::SameLine(0, gap);
    statCard("c5", cw, "PERSONAL BEST", me ? formatLap(me->bestLapMs) : "-",
             me && me->bestLapMs && me->bestLapMs == s.sessionBestLapMs ? C_PURPLE : C_GREEN);
    ImGui::SameLine(0, gap);
    statCard("c6", cw, "SESSION BEST", formatLap(s.sessionBestLapMs), C_PURPLE);
    ImGui::SameLine(0, gap);
    weatherCard(s, 2 * cw + gap, 84);
}
void drawWeather(const Snapshot &s, float w, float h) { weatherCard(s, w, h); }
void drawCar(const Snapshot &s, float w, float h) { carCard(s, w, h); }
void drawRecent(app::App &a, float w, float h) { recentCard(a, w, h); }
void drawMap(app::App &a, const Snapshot &s, float w, float h) { mapCard(a, s, w, h); }
void drawCarStatus(const Snapshot &s, float w, float h) { carStatusCard(s, w, h); }
void drawStandings(app::App &a, const Snapshot &s, float w, float h) { standingsCard(a, s, w, h); }

void setPage(int page) { g_page = std::clamp(page, 0, int(PAGE_COUNT) - 1); }

void setupStyle()
{
    ImGuiStyle &s = ImGui::GetStyle();
    ImGui::StyleColorsDark(&s);
    s.WindowRounding = 0;
    s.ChildRounding = 6;
    s.FrameRounding = 5;
    s.PopupRounding = 6;
    s.GrabRounding = 5;
    s.TabRounding = 5;
    s.ScrollbarRounding = 6;
    s.FramePadding = ImVec2(10, 6);
    s.ItemSpacing = ImVec2(10, 8);
    s.WindowPadding = ImVec2(18, 14);
    s.ScrollbarSize = 12;
    s.SeparatorTextBorderSize = 1;
    s.SeparatorTextPadding = ImVec2(0, 6);
    ImVec4 *c = s.Colors;
    c[ImGuiCol_WindowBg] = v4(C_BG);
    c[ImGuiCol_ChildBg] = v4(0);
    c[ImGuiCol_PopupBg] = v4(C_PANEL);
    c[ImGuiCol_Border] = v4(C_BORDER);
    c[ImGuiCol_Text] = v4(C_TEXT);
    c[ImGuiCol_TextDisabled] = v4(C_DIM);
    c[ImGuiCol_FrameBg] = v4(C_PANEL2);
    c[ImGuiCol_FrameBgHovered] = v4(IM_COL32(40, 46, 56, 255));
    c[ImGuiCol_FrameBgActive] = v4(IM_COL32(48, 55, 66, 255));
    c[ImGuiCol_Button] = v4(C_PANEL2);
    c[ImGuiCol_ButtonHovered] = v4(IM_COL32(44, 51, 62, 255));
    c[ImGuiCol_ButtonActive] = v4(IM_COL32(54, 62, 75, 255));
    c[ImGuiCol_Header] = v4(C_PANEL2);
    c[ImGuiCol_HeaderHovered] = v4(IM_COL32(44, 51, 62, 255));
    c[ImGuiCol_HeaderActive] = v4(IM_COL32(54, 62, 75, 255));
    c[ImGuiCol_CheckMark] = v4(C_ACCENT);
    c[ImGuiCol_SliderGrab] = v4(C_ACCENT);
    c[ImGuiCol_SliderGrabActive] = v4(IM_COL32(255, 80, 80, 255));
    c[ImGuiCol_Separator] = v4(C_BORDER);
    c[ImGuiCol_TableHeaderBg] = v4(0);
    c[ImGuiCol_TableRowBg] = v4(0);
    c[ImGuiCol_TableRowBgAlt] = v4(IM_COL32(255, 255, 255, 6));
    c[ImGuiCol_TableBorderLight] = v4(C_BORDER);
    c[ImGuiCol_ScrollbarBg] = v4(0);
    c[ImGuiCol_ScrollbarGrab] = v4(C_PANEL2);
    c[ImGuiCol_ModalWindowDimBg] = v4(IM_COL32(0, 0, 0, 140));
}

void draw(app::App &a, const Fonts &f, VrEnv &vr)
{
    F = &f;
    const Snapshot snap = a.race.snapshot();
    const ImGuiViewport *vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("main", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImGui::PopStyleVar();
    const bool full = g_page == DASH && g_mapFull; // full-screen map: no sidebar, no top bar
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    if (!full) {
        sidebar(a);
        ImGui::SameLine();
    }
    ImGui::PopStyleVar();

    ImGui::BeginGroup();
    const bool transport = a.source() == Source::Replay && a.replay.isOpen() && g_page != REPLAY && g_page != COMPARE;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, full ? ImVec2(8, 8) : ImVec2(20, 16));
    ImGui::BeginChild("content", ImVec2(0, transport ? -58 : 0), ImGuiChildFlags_AlwaysUseWindowPadding,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();
    if (!full) topBar(a);
    switch (g_page) {
    case DASH: pageDashboard(a, snap); break;
    case SETUP: pageSetup(a, snap); break;
    case COMPARE: pageCompare(a); break;
    case CAPTURE: pageCapture(a); break;
    case REPLAY: pageReplay(a); break;
    case FORWARD: pageForward(a); break;
    case HMD: pageHmd(a, vr); break;
    case SETTINGS: pageSettings(a); break;
    }
    ImGui::EndChild();
    if (transport) bottomTransport(a);
    ImGui::EndGroup();
    ImGui::End();
}

} // namespace ui
