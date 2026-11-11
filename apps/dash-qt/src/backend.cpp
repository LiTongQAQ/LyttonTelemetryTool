#include "backend.hpp"

#include "tracks.hpp"
#include "tyrecol.hpp"
#include "ui.hpp"

#include <windows.h>
#include <commdlg.h>

#include <algorithm>
#include <cmath>
#include <filesystem>

using namespace timing;

namespace {

const QColor C_TEXT(232, 234, 237), C_DIM(135, 144, 157), C_GREEN(46, 214, 120), C_PURPLE(178, 102, 255),
    C_YELLOW(245, 197, 24), C_RED(255, 72, 72);

QColor fromU32(ImU32 c) { return QColor(int(c & 0xFF), int(c >> 8 & 0xFF), int(c >> 16 & 0xFF)); }

QColor teamColour(uint8_t id)
{
    static const QColor c[] = {{39, 244, 210}, {232, 0, 32},   {54, 113, 198}, {100, 196, 255}, {34, 153, 113},
                               {255, 135, 188}, {102, 146, 255}, {182, 186, 189}, {255, 128, 0},  {82, 226, 82}};
    return id < 10 ? c[id] : QColor(120, 120, 120);
}

QColor markColour(Mark m)
{
    switch (m) {
    case Mark::SessionBest: return C_PURPLE;
    case Mark::PersonalBest: return C_GREEN;
    case Mark::Slower: return C_YELLOW;
    case Mark::Invalid: return C_RED;
    default: return C_DIM;
    }
}

void tyreInfo(uint8_t visual, QString &letter, QColor &col)
{
    switch (visual) {
    case 16: letter = "S"; col = QColor(240, 50, 50); return;
    case 17: letter = "M"; col = QColor(250, 210, 40); return;
    case 18: letter = "H"; col = QColor(235, 235, 235); return;
    case 7: letter = "I"; col = QColor(60, 200, 80); return;
    case 8: letter = "W"; col = QColor(50, 130, 255); return;
    default: letter = "?"; col = C_DIM;
    }
}

QString q(const std::string &s) { return QString::fromStdString(s); }

std::string displayName(const Car &c) { return c.ai || c.you ? driverCode(c) : playerCode(c.name); }

std::string gapText(const Snapshot &s, int i, int mode)
{
    const auto &c = s.cars[i];
    if (rankedOnBestLap(s)) {
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

} // namespace

// ---- standings ---------------------------------------------------------------------------------------------------
QVariant StandingsModel::data(const QModelIndex &i, int role) const
{
    if (!i.isValid() || i.row() >= int(rows_.size())) return {};
    const Row &r = rows_[size_t(i.row())];
    switch (role) {
    case Pos: return r.pos;
    case Code: return r.code;
    case Team: return r.team;
    case Gap: return r.gap;
    case Last: return r.last;
    case LastColor: return r.lastColor;
    case Best: return r.best;
    case BestColor: return r.bestColor;
    case Tyre: return r.tyre;
    case TyreColor: return r.tyreColor;
    case Age: return r.age;
    case InPit: return r.inPit;
    case You: return r.you;
    case Lapped: return r.lapped;
    }
    return {};
}

QHash<int, QByteArray> StandingsModel::roleNames() const
{
    return {{Pos, "pos"},   {Code, "code"},       {Team, "team"},   {Gap, "gap"},     {Last, "last"},
            {LastColor, "lastColor"}, {Best, "best"}, {BestColor, "bestColor"}, {Tyre, "tyre"},
            {TyreColor, "tyreColor"}, {Age, "age"}, {InPit, "inPit"}, {You, "you"}, {Lapped, "lapped"}};
}

void StandingsModel::set(std::vector<Row> rows)
{
    if (rows.size() != rows_.size()) {
        beginResetModel();
        rows_ = std::move(rows);
        endResetModel();
        return;
    }
    rows_ = std::move(rows);
    if (!rows_.empty()) emit dataChanged(index(0), index(int(rows_.size()) - 1));
}

// ---- backend -----------------------------------------------------------------------------------------------------
Backend::Backend(engine::Engine &e, int startPage, QObject *parent) : QObject(parent), e_(e), startPage_(startPage) {}

QString Backend::version() const
{
#ifdef F1RACE_VERSION
    return F1RACE_VERSION;
#else
    return "dev";
#endif
}

QString Backend::sourceName() const
{
    switch (e_.a.source()) {
    case app::Source::Live: return "LIVE";
    case app::Source::Sim: return "SIMULATION";
    case app::Source::Replay: return "REPLAY";
    }
    return "";
}

void Backend::setGapMode(int m)
{
    if (m == e_.a.cfg.gapMode) return;
    e_.a.cfg.gapMode = std::clamp(m, 0, 2);
    e_.a.dirty = true;
    emit updated();
}

int Backend::page() const { return ui::page(); }
void Backend::setPage(int p) { ui::setPage(p); emit updated(); }
bool Backend::mapFull() const { return ui::mapFull(); }
void Backend::setMapFull(bool on) { ui::setMapFull(on); emit updated(); }
bool Backend::transport() const { return ui::transportShown(e_.a); }

void Backend::useLive() { e_.a.useLive(); emit updated(); }
void Backend::useSim() { e_.a.useSim(); emit updated(); }

void Backend::openRecording()
{
    wchar_t file[MAX_PATH] = L"";
    const std::wstring dir = std::filesystem::path(e_.a.recordingsDir()).wstring();
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof ofn;
    ofn.lpstrFilter = L"F1 telemetry recording (*.f1tr)\0*.f1tr\0All files\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrInitialDir = dir.c_str();
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetOpenFileNameW(&ofn)) e_.a.useReplay(std::filesystem::path(file).string());
    emit updated();
}

void Backend::toggleRecording()
{
    if (e_.a.recording()) e_.a.stopRecording();
    else if (e_.a.canRecord()) e_.a.startRecording();
    emit updated();
}

void Backend::refresh()
{
    const Snapshot &s = e_.snap;
    const auto &p = s.player;
    const Car *me = s.playerIdx < f1tel::MAX_CARS ? &s.cars[s.playerIdx] : nullptr;
    const auto order = s.order();

    // session
    trackName_ = s.session.trackId >= 0 ? q(tracks::asciiName(s.session.trackId)) : QString();
    sessionName_ = s.session.valid ? q(sessionTypeName(s.session.sessionType)) : QString();
    totalLaps_ = s.session.totalLaps;
    lap_ = me ? me->lapNum : 0;
    position_ = me ? me->position : 0;
    numCars_ = int(order.size());

    // lap times
    currentLap_ = me ? q(formatLap(me->currentLapMs)) : "-";
    currentInvalid_ = me && me->currentLapInvalid;
    lastLap_ = me ? q(formatLap(me->lastLapMs)) : "-";
    {
        Mark m = me && !me->laps.empty() ? markLap(s, s.playerIdx, me->lastLapMs, me->laps.back().lapValid) : Mark::None;
        lastLapColor_ = m == Mark::None ? C_TEXT : markColour(m);
    }
    bestLap_ = me ? q(formatLap(me->bestLapMs)) : "-";
    bestLapColor_ = me && me->bestLapMs && me->bestLapMs == s.sessionBestLapMs ? C_PURPLE : C_GREEN;
    sessionBest_ = q(formatLap(s.sessionBestLapMs));

    // sectors: the running lap's completed sectors; after the line the finished lap's three stay a few seconds,
    // then all three reset together (timing::lapSectors)
    sectors_.clear();
    std::array<timing::SectorCell, 3> cells{};
    if (me) cells = lapSectors(s, s.playerIdx);
    for (int k = 0; k < 3; k++) {
        QVariantMap m;
        m["label"] = QString("S%1").arg(k + 1);
        const auto &cell = cells[size_t(k)];
        const bool done = cell.state == timing::SectorCell::Done || cell.state == timing::SectorCell::Held;
        const uint32_t ms = done ? cell.ms : 0;
        const QColor col = done ? markColour(cell.mark) : C_DIM;
        const bool running = cell.state == timing::SectorCell::Running;
        m["time"] = running && me ? QString(runningLabel(*me)) : ms ? q(formatSector(ms)) : QString("-");
        m["color"] = col;
        m["done"] = done;
        sectors_.push_back(m);
    }

    // car
    speed_ = p.speed;
    gear_ = p.gear < 0 ? "R" : p.gear == 0 ? "N" : QString::number(p.gear);
    rpm_ = p.rpm;
    revLights_ = p.revLights / 100.0;
    throttle_ = p.throttle;
    brake_ = p.brake;
    steer_ = p.steer;
    ers_ = std::clamp(p.ersJ / 4.0e6f, 0.f, 1.f);
    ersFault_ = s.damage.ersFault;
    static const char *ersModes[4] = {"NONE", "MEDIUM", "HOTLAP", "OVERTAKE"};
    ersMode_ = p.ersMode < 4 ? ersModes[p.ersMode] : "?";
    drsState_ = s.damage.drsFault ? 3 : p.drs ? 2 : p.drsAllowed ? 1 : 0;

    const auto &f = s.fuel;
    auto num = [](float v, const char *fmt) { char b[32]; std::snprintf(b, sizeof b, fmt, v); return QString(b); };
    fuel_.clear();
    fuel_["kg"] = num(p.fuelKg, "%.2f kg");
    fuel_["laps"] = num(p.fuelLaps, "%+.1f LAPS");
    fuel_["lapsNegative"] = p.fuelLaps < 0;
    fuel_["now"] = num(f.kgPerS * 1000, "%.1f g/s");
    fuel_["per100"] = f.kgPer100km > 0 ? num(f.kgPer100km, "%.0f kg") : "-";
    fuel_["thisLap"] = num(f.lapUsedKg, "%.2f kg");
    fuel_["lastLap"] = f.lastLapKg > 0 ? num(f.lastLapKg, "%.2f kg") : "-";
    fuel_["avg"] = f.avgLapKg > 0 ? num(f.avgLapKg, "%.2f kg") : "-";
    fuel_["left"] = f.avgLapKg > 0 ? num(p.fuelKg / f.avgLapKg, "%.1f") : "-";

    // tyres, shown FL FR RL RR (the game's order is RL RR FL FR)
    const uint8_t visual = s.config.valid ? s.config.visualTyre : me ? me->visualTyre : 0;
    tyres_.clear();
    for (int w : {2, 3, 0, 1}) {
        QVariantMap m;
        m["inner"] = p.tyreInner[size_t(w)];
        m["surface"] = p.tyreSurface[size_t(w)];
        m["wear"] = double(p.tyreWear[size_t(w)]);
        m["brake"] = p.brakeTemp[size_t(w)];
        m["innerColor"] = fromU32(tyrecol::temp(p.tyreInner[size_t(w)], visual));
        m["wearColor"] = fromU32(tyrecol::wear(p.tyreWear[size_t(w)]));
        m["brakeColor"] = fromU32(tyrecol::brake(p.brakeTemp[size_t(w)]));
        tyres_.push_back(m);
    }
    tyreInfo(visual, compound_, compoundColor_);
    tyreAge_ = s.config.valid ? s.config.tyreAge : me ? me->tyreAgeLaps : 0;

    // standings
    const SessionGroup grp = sessionGroup(s.session.sessionType);
    const float L = std::max(1.f, float(s.session.trackLength));
    std::vector<StandingsModel::Row> rows;
    rows.reserve(order.size());
    for (int i : order) {
        const Car &c = s.cars[size_t(i)];
        StandingsModel::Row r;
        r.pos = c.position;
        r.code = q(displayName(c));
        r.team = teamColour(c.teamId);
        r.gap = q(gapText(s, i, e_.a.cfg.gapMode));
        r.last = q(formatLap(c.lastLapMs));
        const Mark lm = c.laps.empty() ? Mark::None : markLap(s, i, c.lastLapMs, c.laps.back().lapValid);
        r.lastColor = lm == Mark::SessionBest ? C_PURPLE : lm == Mark::Invalid ? C_RED : C_TEXT;
        r.best = q(formatLap(c.bestLapMs));
        r.bestColor = c.bestLapMs && c.bestLapMs == s.sessionBestLapMs ? C_PURPLE : C_DIM;
        tyreInfo(c.visualTyre, r.tyre, r.tyreColor);
        r.age = c.tyreAgeLaps;
        r.inPit = c.pitStatus != 0;
        r.you = i == s.playerIdx;
        if (me && !r.you && (grp == SessionGroup::Race || grp == SessionGroup::Sprint))
            r.lapped = (me->lapNum + me->lapDistance / L) - (c.lapNum + c.lapDistance / L) >= 1.f - 300.f / L;
        rows.push_back(std::move(r));
    }
    standings_.set(std::move(rows));
    emit updated();
}
