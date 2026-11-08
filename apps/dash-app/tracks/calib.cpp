#include "calib.hpp"

#include "replay.hpp"

#include <f1tel/parse.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <random>
#include <tuple>
#include <unordered_set>

namespace tracks {

namespace {

constexpr int EDGE_CAP = 24; // edge points of one kind kept per bin and side

// Wheel positions relative to the car's reference point (m): F1 cars, roughly. Order as in the packets: RL RR FL FR.
constexpr float WHEEL_FWD[4] = {-1.75f, -1.75f, 1.85f, 1.85f};
constexpr float WHEEL_RIGHT[4] = {-0.8f, 0.8f, -0.8f, 0.8f};

struct CarNow {
    bool lap = false, motion = false;
    float dist = -1;
    uint8_t pit = 0, driver = 0, result = 0;
    float x = 0, z = 0, fx = 0, fz = 1, rx = 1, rz = 0;
    // each wheel at the previous CarTelemetry: surface (255 = none yet), position, lap distance
    uint8_t surf[4] = {255, 255, 255, 255};
    float wx[4] = {}, wz[4] = {}, wd[4] = {};
};

// 0 = track (tarmac), 1 = kerb, 2 = off the track (rock, gravel, mud, sand, grass, water), 3 = neither (concrete,
// cobbles, metal, ridged: run-offs and pit areas, not a clear edge)
int surfaceClass(uint8_t s)
{
    using f1tel::Surface;
    if (s == uint8_t(Surface::Tarmac)) return 0;
    if (s == uint8_t(Surface::RumbleStrip)) return 1;
    if (s >= uint8_t(Surface::Rock) && s <= uint8_t(Surface::Water)) return 2;
    return 3;
}

// The kind of edge a change of surface marks, or 0 for none.
char crossing(uint8_t from, uint8_t to)
{
    const int a = surfaceClass(from), b = surfaceClass(to);
    if (a == b || a == 3 || b == 3) return 0;
    if (a == 2 || b == 2) return 'O'; // track or kerb <-> grass, gravel ...
    return 'I';                       // tarmac <-> kerb
}

bool onTrack(const CarNow &c, float length)
{
    // driverStatus 0 = in the garage; pitStatus 1 / 2 = pitting / in the pit area; resultStatus 2 = active
    return c.lap && c.motion && c.driver != 0 && c.pit == 0 && c.result == 2 && c.dist >= 0 && c.dist < length;
}

} // namespace

float CalData::coverage() const
{
    if (bins.empty()) return 0;
    size_t n = 0;
    for (const auto &b : bins) n += b.n > 0;
    return float(n) / float(bins.size());
}

// ---- scanner -------------------------------------------------------------------------------------------------------
struct Scanner::Impl {
    std::map<int, CalData> out;
    std::mt19937_64 rng{12345};
    // per track: edge points per bin, by kind (K, I, O) and side (L, R)
    std::map<int, std::vector<std::array<uint8_t, 6>>> kerbCount;
    std::string name;
    int trackId = -1, gameYear = 0;
    float length = 0;
    CarNow cars[f1tel::MAX_CARS];
    std::map<int, bool> tracksSeen;
    // clean-up for recordings made through a forwarding loop, see feed()
    std::unordered_set<uint64_t> seen;
    std::vector<uint64_t> ring = std::vector<uint64_t>(8192, 0);
    size_t ringPos = 0;
    uint32_t lastFrame[16] = {};
    uint32_t lapFrame = 0, motionFrame = 0, frame = 0;
    int player = -1;
    std::vector<uint64_t> marked; // cells marked by the current car in the current frame

    CalData *data()
    {
        if (trackId < 0 || length < 500) return nullptr;
        auto &d = out[trackId];
        if (d.trackId < 0) {
            d.trackId = trackId;
            d.trackLength = length;
            d.bins.assign(size_t(std::ceil(length / CAL_BIN_M)), {});
        }
        if (gameYear) d.gameYear = gameYear;
        if (!tracksSeen[trackId]) {
            tracksSeen[trackId] = true;
            if (!name.empty()) d.sources.push_back(name);
        }
        return &d;
    }

    // the surface scan: adds `w` hits of class `cls` to the cell at (x, z), once per car and frame
    void mark(CalData &d, float x, float z, int cls, uint16_t w)
    {
        const uint64_t key = (uint64_t(uint32_t(int32_t(std::floor(x / CAL_CELL_M)))) << 32) |
                             uint32_t(int32_t(std::floor(z / CAL_CELL_M)));
        if (std::find(marked.begin(), marked.end(), key) != marked.end()) return;
        marked.push_back(key);
        uint16_t &c = d.grid[key][cls];
        c = uint16_t(std::min(65535, c + w));
    }
    // a wheel's path since the last frame: the first half has the old surface, the second the new one
    void sweepLine(CalData &d, float x0, float z0, float x1, float z1, int c0, int c1, uint16_t w)
    {
        const float len = std::hypot(x1 - x0, z1 - z0);
        const int steps = std::max(1, int(len / (CAL_CELL_M * 0.5f)));
        for (int s = 0; s <= steps; s++) {
            const float t = float(s) / float(steps);
            mark(d, x0 + (x1 - x0) * t, z0 + (z1 - z0) * t, t < 0.5f ? c0 : c1, w);
        }
    }
    // the ground between an axle's two wheels, from the last frame to this one (a quad): tarmac when all four wheel
    // positions were on tarmac
    void sweepQuad(CalData &d, const float a[2], const float b[2], const float c[2], const float e[2], uint16_t w)
    {
        // a-b: the axle now (left, right), e-c: the axle at the last frame (left, right)
        const float across = std::max(std::hypot(b[0] - a[0], b[1] - a[1]), std::hypot(c[0] - e[0], c[1] - e[1]));
        const float along = std::max(std::hypot(e[0] - a[0], e[1] - a[1]), std::hypot(c[0] - b[0], c[1] - b[1]));
        const int nu = std::max(1, int(across / (CAL_CELL_M * 0.5f))), nv = std::max(1, int(along / (CAL_CELL_M * 0.5f)));
        for (int v = 0; v <= nv; v++) {
            const float tv = float(v) / float(nv);
            const float lx = a[0] + (e[0] - a[0]) * tv, lz = a[1] + (e[1] - a[1]) * tv;
            const float rx = b[0] + (c[0] - b[0]) * tv, rz = b[1] + (c[1] - b[1]) * tv;
            for (int u = 0; u <= nu; u++) {
                const float tu = float(u) / float(nu);
                mark(d, lx + (rx - lx) * tu, lz + (rz - lz) * tu, S_TARMAC, w);
            }
        }
    }
};

Scanner::Scanner() : m(std::make_unique<Impl>()) {}
Scanner::~Scanner() = default;

void Scanner::begin(const std::string &source)
{
    auto &s = *m;
    s.name = source;
    s.trackId = -1;
    s.gameYear = 0;
    s.length = 0;
    for (auto &c : s.cars) c = {};
    s.tracksSeen.clear();
    s.seen.clear();
    std::fill(s.ring.begin(), s.ring.end(), 0);
    s.ringPos = 0;
    std::fill(std::begin(s.lastFrame), std::end(s.lastFrame), 0);
    s.lapFrame = s.motionFrame = s.frame = 0;
    s.player = -1;
    player = {};
    added = kerbs = dropped = 0;
}

int Scanner::trackId() const { return m->trackId; }
std::map<int, CalData> &Scanner::results() { return m->out; }
const std::vector<std::array<uint8_t, 6>> *Scanner::edgeCounts(int trackId) const
{
    auto it = m->kerbCount.find(trackId);
    return it == m->kerbCount.end() ? nullptr : &it->second;
}

void Scanner::feed(const uint8_t *p, size_t n)
{
    using namespace f1tel;
    auto &s = *m;
    auto near = [](uint32_t a, uint32_t b) { return (a > b ? a - b : b - a) <= 2; };
    const auto h = read_header(p, n);
    if (!h) return;
    if (!h.header.sessionUID) { dropped++; return; } // not part of a session
    s.gameYear = h.header.gameYear;
    s.player = h.header.playerCarIndex < MAX_CARS ? h.header.playerCarIndex : -1;
    // Clean-up for recordings made through a forwarding loop: the same packets many times over and out of order.
    // Exact repeats are dropped, and so is any packet older (overall frame number, which never goes back, not even
    // after a flashback) than the newest one of its type. Positions are paired with lap distances only from the
    // same frame.
    uint64_t hash = 1469598103934665603ull ^ n;
    for (size_t i = 0; i < n; i++) hash = (hash ^ p[i]) * 1099511628211ull;
    if (!s.seen.insert(hash).second) { dropped++; return; }
    if (s.ring[s.ringPos]) s.seen.erase(s.ring[s.ringPos]);
    s.ring[s.ringPos] = hash;
    s.ringPos = (s.ringPos + 1) % s.ring.size();
    s.frame = h.header.overallFrameIdentifier;
    uint32_t &lf = s.lastFrame[h.header.packetId & 15];
    if (s.frame < lf) { dropped++; return; }
    lf = s.frame;
    switch (PacketId(h.header.packetId)) {
    case PacketId::Session: {
        auto ps = parse<PacketSession>(p, n);
        if (!ps) break;
        if (ps->trackId != s.trackId) for (auto &c : s.cars) c = {};
        s.trackId = ps->trackId;
        s.length = ps->trackLength;
        break;
    }
    case PacketId::LapData: {
        auto ps = parse<PacketLapData>(p, n);
        if (!ps) break;
        for (int i = 0; i < MAX_CARS; i++) {
            const auto &l = ps->car[i];
            auto &c = s.cars[i];
            c.lap = true;
            c.dist = l.lapDistance;
            c.pit = l.pitStatus;
            c.driver = l.driverStatus;
            c.result = l.resultStatus;
        }
        s.lapFrame = s.frame;
        break;
    }
    case PacketId::Motion: {
        auto ps = parse<PacketMotion>(p, n);
        if (!ps) break;
        s.motionFrame = s.frame;
        CalData *d = near(s.frame, s.lapFrame) ? s.data() : nullptr; // positions only with this frame's distances
        for (int i = 0; i < MAX_CARS; i++) {
            const auto &mo = ps->car[i];
            auto &c = s.cars[i];
            c.motion = true;
            c.x = mo.worldPositionX;
            c.z = mo.worldPositionZ;
            c.fx = mo.worldForwardDirX / 32767.f;
            c.fz = mo.worldForwardDirZ / 32767.f;
            c.rx = mo.worldRightDirX / 32767.f;
            c.rz = mo.worldRightDirZ / 32767.f;
            if (!d || !onTrack(c, s.length)) continue;
            const size_t bi = std::min(d->bins.size() - 1, size_t(c.dist / CAL_BIN_M));
            auto &b = d->bins[bi];
            b.sx += c.x;
            b.sz += c.z;
            b.n++;
            // reservoir sample: every position of the bin has the same chance to be kept
            if (d->pos.size() < d->bins.size()) d->pos.resize(d->bins.size());
            auto &pv = d->pos[bi];
            if (pv.size() < size_t(CAL_POS_CAP)) pv.push_back({c.x, c.z});
            else if (const uint64_t r = s.rng() % b.n; r < uint64_t(CAL_POS_CAP)) pv[size_t(r)] = {c.x, c.z};
            d->samples++;
            added++;
        }
        break;
    }
    case PacketId::CarTelemetry: {
        auto ps = parse<PacketCarTelemetry>(p, n);
        CalData *d = near(s.frame, s.lapFrame) && near(s.frame, s.motionFrame) ? s.data() : nullptr;
        if (!ps || !d) break;
        auto &cnt = s.kerbCount[s.trackId];
        if (cnt.size() < d->bins.size()) cnt.resize(d->bins.size(), {0, 0, 0, 0, 0, 0});
        const float length = s.length;
        // cap per bin, kind and side: a car riding a long kerb would otherwise flood it
        auto keep = [&](float dist, float x, float z, char side, char kind) {
            if (dist < 0 || dist >= length) return;
            const size_t bin = std::min(d->bins.size() - 1, size_t(dist / CAL_BIN_M));
            const int k = (kind == 'K' ? 0 : kind == 'I' ? 2 : 4) + (side == 'R');
            if (cnt[bin][k] >= EDGE_CAP) return;
            cnt[bin][k]++;
            d->edges.push_back({dist, x, z, side, kind});
            kerbs++;
        };
        for (int i = 0; i < MAX_CARS; i++) {
            auto &c = s.cars[i];
            const uint8_t *surf = &ps->car[i].surfaceType.rl;
            if (i == s.player) {
                player.valid = c.motion;
                player.onTrack = onTrack(c, length);
                player.x = c.x;
                player.z = c.z;
                player.fx = c.fx;
                player.fz = c.fz;
                player.dist = c.dist;
                player.speed = ps->car[i].speed;
                for (int w = 0; w < 4; w++) player.surf[w] = surf[w];
            }
            if (!onTrack(c, length)) {
                for (int w = 0; w < 4; w++) c.surf[w] = 255;
                continue;
            }
            float wx[4], wz[4], wd[4];
            bool moved[4]; // the wheel's last position is this frame's neighbour (not a teleport or a new lap)
            for (int w = 0; w < 4; w++) {
                wx[w] = c.x + c.fx * WHEEL_FWD[w] + c.rx * WHEEL_RIGHT[w];
                wz[w] = c.z + c.fz * WHEEL_FWD[w] + c.rz * WHEEL_RIGHT[w];
                wd[w] = c.dist + WHEEL_FWD[w];
                moved[w] = c.surf[w] != 255 && std::fabs(wd[w] - c.wd[w]) < 10.f &&
                           std::hypot(wx[w] - c.wx[w], wz[w] - c.wz[w]) < 8.f;
            }
            // The surface scan, as an area: each wheel's path since the last frame (direct contact, weight 2), and
            // the ground between the two wheels of an axle where both were on tarmac then and now (weight 1).
            s.marked.clear();
            for (int w = 0; w < 4; w++) {
                const int now = surfaceClassOf(surf[w]);
                if (moved[w]) s.sweepLine(*d, c.wx[w], c.wz[w], wx[w], wz[w], surfaceClassOf(c.surf[w]), now, 2);
                else s.mark(*d, wx[w], wz[w], now, 2);
            }
            const uint8_t T = uint8_t(Surface::Tarmac);
            for (int ax = 0; ax < 4; ax += 2) { // RL RR, FL FR
                const int l = ax, r = ax + 1;
                if (surf[l] != T || surf[r] != T) continue;
                const float a[2] = {wx[l], wz[l]}, b[2] = {wx[r], wz[r]};
                if (moved[l] && moved[r] && c.surf[l] == T && c.surf[r] == T) {
                    const float pr[2] = {c.wx[r], c.wz[r]}, pl[2] = {c.wx[l], c.wz[l]};
                    s.sweepQuad(*d, a, b, pr, pl, 1);
                } else {
                    s.sweepQuad(*d, a, b, b, a, 1);
                }
            }
            for (int w = 0; w < 4; w++) {
                const char side = WHEEL_RIGHT[w] < 0 ? 'L' : 'R';
                // a change of surface since the last frame: the edge is between the two wheel positions
                if (moved[w])
                    if (const char kind = crossing(c.surf[w], surf[w]))
                        keep((wd[w] + c.wd[w]) / 2, (wx[w] + c.wx[w]) / 2, (wz[w] + c.wz[w]) / 2, side, kind);
                if (surf[w] == uint8_t(Surface::RumbleStrip)) keep(wd[w], wx[w], wz[w], side, 'K');
                c.surf[w] = surf[w];
                c.wx[w] = wx[w];
                c.wz[w] = wz[w];
                c.wd[w] = wd[w];
            }
        }
        break;
    }
    default: break;
    }
}

std::vector<CalData> extract(const std::vector<std::string> &recordings, ExtractProgress *progress,
                             std::vector<std::string> *log)
{
    Scanner sc;
    if (progress) progress->filesTotal = int(recordings.size());
    for (const auto &path : recordings) {
        if (progress && progress->cancel) break;
        const std::string name = std::filesystem::path(path).filename().string();
        sc.begin(name);
        f1hub::Replayer rp([&](const uint8_t *p, size_t n) { sc.feed(p, n); }, [] {});
        std::string err;
        if (!rp.open(path, &err)) {
            if (log) log->push_back(name + "：打不开（" + err + "）");
        } else {
            rp.seek(rp.duration()); // feeds every packet on this thread
            if (log) {
                char b[240];
                const int id = sc.trackId();
                std::snprintf(b, sizeof b, "%s：%s，%llu 个位置点，%llu 个路肩点", name.c_str(),
                              id >= 0 ? asciiName(id).c_str() : "没有场次信息", (unsigned long long)sc.added,
                              (unsigned long long)sc.kerbs);
                std::string line = b;
                if (sc.dropped) { // a polluted recording (forwarding loop)
                    std::snprintf(b, sizeof b, "；丢弃 %llu 个重复或乱序的包（共 %zu 个）", (unsigned long long)sc.dropped,
                                  size_t(rp.packets()));
                    line += b;
                }
                log->push_back(line);
            }
        }
        if (progress) progress->filesDone++;
    }
    std::vector<CalData> v;
    for (auto &[id, d] : sc.results())
        if (d.samples) v.push_back(std::move(d));
    return v;
}

// ---- data files ---------------------------------------------------------------------------------------------------
// Text, so files from different people can be looked at and merged:
//   f1race-trackcal 1 / track <id> / length <m> / source <name> ... / c <bin> <sum x> <sum z> <n> / e <dist> <x> <z> <L|R>
namespace {
void writeData(std::ostream &f, const CalData &d)
{
    f << "f1race-trackcal 1\ntrack " << d.trackId << "\nlength " << d.trackLength << "\nsamples " << d.samples << "\n";
    if (d.gameYear) f << "game " << d.gameYear << "\n";
    for (const auto &s : d.sources) f << "source " << s << "\n";
    char b[128];
    for (size_t i = 0; i < d.bins.size(); i++) {
        const auto &c = d.bins[i];
        if (!c.n) continue;
        std::snprintf(b, sizeof b, "c %zu %.3f %.3f %u\n", i, c.sx, c.sz, c.n);
        f << b;
    }
    for (const auto &e : d.edges) {
        std::snprintf(b, sizeof b, "e %.2f %.3f %.3f %c %c\n", e.dist, e.x, e.z, e.side, e.kind);
        f << b;
    }
    for (const auto &[key, v] : d.grid) {
        f << "g " << int32_t(uint32_t(key >> 32)) << " " << int32_t(uint32_t(key));
        for (uint16_t n : v) f << " " << n;
        f << "\n";
    }
    for (size_t i = 0; i < d.pos.size(); i++)
        for (const auto &p : d.pos[i]) {
            std::snprintf(b, sizeof b, "p %zu %.2f %.2f\n", i, p[0], p[1]);
            f << b;
        }
}

// Reads data lines until the end of the stream or a line `stop`.
bool readData(std::istream &f, CalData &d, std::string *error, const char *stop = nullptr)
{
    d = {};
    std::string line;
    if (!std::getline(f, line) || line.rfind("f1race-trackcal", 0) != 0) {
        if (error) *error = "不是配准数据文件";
        return false;
    }
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (stop && line == stop) break;
        std::istringstream is(line);
        std::string k;
        is >> k;
        if (k == "track") is >> d.trackId;
        else if (k == "length") {
            is >> d.trackLength;
            d.bins.assign(size_t(std::ceil(d.trackLength / CAL_BIN_M)), {});
        } else if (k == "samples") is >> d.samples;
        else if (k == "game") is >> d.gameYear;
        else if (k == "source") d.sources.push_back(line.size() > 7 ? line.substr(7) : "");
        else if (k == "c") {
            size_t i;
            CalBin c;
            if (is >> i >> c.sx >> c.sz >> c.n && i < d.bins.size()) d.bins[i] = c;
        } else if (k == "g") {
            int32_t x, z;
            std::array<uint16_t, S_CLASSES> v{};
            if (is >> x >> z) {
                for (auto &n : v) is >> n;
                d.grid[(uint64_t(uint32_t(x)) << 32) | uint32_t(z)] = v;
            }
        } else if (k == "p") {
            size_t i;
            float x, z;
            if (is >> i >> x >> z && i < d.bins.size()) {
                if (d.pos.size() < d.bins.size()) d.pos.resize(d.bins.size());
                d.pos[i].push_back({x, z});
            }
        } else if (k == "e") {
            CalEdge e;
            if (is >> e.dist >> e.x >> e.z >> e.side) {
                if (!(is >> e.kind)) e.kind = 'K'; // files from before the kinds: kerb contacts
                d.edges.push_back(e);
            }
        }
    }
    if (d.trackId < 0 || d.bins.empty()) {
        if (error) *error = "文件里没有赛道信息";
        return false;
    }
    return true;
}
} // namespace

bool saveData(const CalData &d, const std::string &path, std::string *error)
{
    std::ofstream f(std::filesystem::path(path), std::ios::binary);
    if (!f) { if (error) *error = "写不了文件"; return false; }
    writeData(f, d);
    return bool(f);
}

bool loadData(const std::string &path, CalData &d, std::string *error)
{
    std::ifstream f(std::filesystem::path(path), std::ios::binary);
    if (!f) { if (error) *error = "打不开"; return false; }
    return readData(f, d, error);
}

bool mergeData(CalData &a, const CalData &b)
{
    if (a.trackId < 0) { a = b; return true; }
    if (a.trackId != b.trackId) return false;
    if (b.bins.size() > a.bins.size()) a.bins.resize(b.bins.size());
    for (size_t i = 0; i < b.bins.size(); i++) {
        a.bins[i].sx += b.bins[i].sx;
        a.bins[i].sz += b.bins[i].sz;
        a.bins[i].n += b.bins[i].n;
    }
    a.edges.insert(a.edges.end(), b.edges.begin(), b.edges.end());
    for (const auto &[key, v] : b.grid) {
        auto &t = a.grid[key];
        for (int k = 0; k < S_CLASSES; k++) t[k] = uint16_t(std::min(65535, int(t[k]) + int(v[k])));
    }
    // position samples: both, then thinned back to the cap with the same chance for each
    if (a.pos.size() < b.pos.size()) a.pos.resize(b.pos.size());
    std::mt19937 rng(777);
    for (size_t i = 0; i < b.pos.size(); i++) {
        auto &v = a.pos[i];
        v.insert(v.end(), b.pos[i].begin(), b.pos[i].end());
        if (v.size() > size_t(CAL_POS_CAP)) {
            std::shuffle(v.begin(), v.end(), rng);
            v.resize(CAL_POS_CAP);
        }
    }
    a.samples += b.samples;
    if (!a.gameYear) a.gameYear = b.gameYear;
    a.sources.insert(a.sources.end(), b.sources.begin(), b.sources.end());
    a.trackLength = std::max(a.trackLength, b.trackLength);
    return true;
}

// ---- calibration ----------------------------------------------------------------------------------------------------
namespace {

struct Pair {
    double mx, mz, tx, tz, w; // map point -> target, weight
};

// Weighted 2D similarity (rotation + uniform scale + translation) taking the map points onto the targets.
struct Sim {
    double c = 1, s = 0, tx = 0, tz = 0; // x' = c x - s z + tx, z' = s x + c z + tz
    void apply(float &x, float &z) const
    {
        const double nx = c * x - s * z + tx, nz = s * x + c * z + tz;
        x = float(nx);
        z = float(nz);
    }
};

Sim solve(const std::vector<Pair> &ps)
{
    Sim r;
    double W = 0, mx = 0, mz = 0, tx = 0, tz = 0;
    for (const auto &p : ps) { W += p.w; mx += p.w * p.mx; mz += p.w * p.mz; tx += p.w * p.tx; tz += p.w * p.tz; }
    if (W <= 0) return r;
    mx /= W; mz /= W; tx /= W; tz /= W;
    double a = 0, b = 0, n2 = 0;
    for (const auto &p : ps) {
        const double px = p.mx - mx, pz = p.mz - mz, qx = p.tx - tx, qz = p.tz - tz;
        a += p.w * (px * qx + pz * qz);
        b += p.w * (px * qz - pz * qx);
        n2 += p.w * (px * px + pz * pz);
    }
    if (n2 < 1e-9) return r;
    r.c = a / n2;
    r.s = b / n2;
    // keep the scale close to the first alignment: kerbs may sit a little inside or outside the edge
    const double sc = std::hypot(r.c, r.s);
    if (sc > 1.03 || sc < 0.97) { const double k = std::clamp(sc, 0.97, 1.03) / sc; r.c *= k; r.s *= k; }
    r.tx = tx - (r.c * mx - r.s * mz);
    r.tz = tz - (r.s * mx + r.c * mz);
    return r;
}

void applyAll(Geometry &g, const Sim &t)
{
    for (size_t i = 0; i < g.cx.size(); i++) {
        t.apply(g.cx[i], g.cz[i]);
        t.apply(g.lx[i], g.lz[i]);
        t.apply(g.rx[i], g.rz[i]);
    }
}

// Index of the outline point at lap distance d (the outline is ordered by distance along it, not by g.dist)
struct DistIndex {
    std::vector<std::pair<float, int>> byDist;
    float length = 1;
    explicit DistIndex(const Geometry &g, float len) : length(len)
    {
        for (size_t i = 0; i < g.dist.size(); i++) byDist.push_back({g.dist[i], int(i)});
        std::sort(byDist.begin(), byDist.end());
    }
    int at(float d) const
    {
        d = std::fmod(d, length);
        if (d < 0) d += length;
        auto it = std::lower_bound(byDist.begin(), byDist.end(), std::make_pair(d, -1));
        if (it == byDist.end()) it = byDist.begin();
        return it->second;
    }
};

// Nearest point on either edge (on the edge's segments, not just its vertices) within `window` outline points of
// index i. Also gives the outward normal there (from the centre line towards that edge). Returns the distance (m).
struct EdgeHit {
    float dist = 1e9f;
    double px = 0, pz = 0; // the point on the edge
    double nx = 0, nz = 0; // outward
};
EdgeHit nearestEdge(const Geometry &g, int i, int window, float x, float z)
{
    const int n = int(g.cx.size());
    EdgeHit h;
    for (int k = -window; k <= window; k++) {
        const int j = ((i + k) % n + n) % n, j2 = (j + 1) % n;
        for (int side = 0; side < 2; side++) {
            const float ax = side ? g.rx[j] : g.lx[j], az = side ? g.rz[j] : g.lz[j];
            const float bx = (side ? g.rx[j2] : g.lx[j2]) - ax, bz = (side ? g.rz[j2] : g.lz[j2]) - az;
            const float l2 = bx * bx + bz * bz;
            const float u = l2 > 1e-6f ? std::clamp(((x - ax) * bx + (z - az) * bz) / l2, 0.f, 1.f) : 0.f;
            const float px = ax + u * bx, pz = az + u * bz;
            const float d = std::hypot(px - x, pz - z);
            if (d < h.dist) {
                h.dist = d;
                h.px = px;
                h.pz = pz;
                const double ox = px - g.cx[j], oz = pz - g.cz[j], ol = std::max(1e-6, std::hypot(ox, oz));
                h.nx = ox / ol;
                h.nz = oz / ol;
            }
        }
    }
    return h;
}

int kindIndex(char k) { return k == 'O' ? 0 : k == 'I' ? 1 : 2; }

// Where an edge point should be on the outline: its nearest edge point moved out by the offset of its kind.
struct Target {
    bool ok = false;
    double mx = 0, mz = 0; // map point
    float signedOut = 0;   // edge point's distance outside the edge (before the offset)
};
Target target(const Geometry &g, const DistIndex &ix, const CalEdge &e, const float off[3], float gate)
{
    Target t;
    const EdgeHit h = nearestEdge(g, ix.at(e.dist), 25, e.x, e.z);
    if (h.dist > gate) return t;
    const float o = off[kindIndex(e.kind)];
    t.ok = true;
    t.mx = h.px + h.nx * o;
    t.mz = h.pz + h.nz * o;
    t.signedOut = float((e.x - h.px) * h.nx + (e.z - h.pz) * h.nz);
    return t;
}

float kerbRms(const Geometry &g, const DistIndex &ix, const std::vector<CalEdge> &edges, const float off[3], float gate)
{
    double s = 0;
    int n = 0;
    for (const auto &e : edges) {
        const Target t = target(g, ix, e, off, gate);
        if (!t.ok) continue;
        const double d = std::hypot(e.x - t.mx, e.z - t.mz);
        s += d * d;
        n++;
    }
    return n ? float(std::sqrt(s / n)) : 0.f;
}

// Median of the points' distances outside the edge, per kind: how far that kind of point sits from the outline's edge.
void estimateOffsets(const Geometry &g, const DistIndex &ix, const std::vector<CalEdge> &edges, float gate, float off[3])
{
    const float zero[3] = {0, 0, 0};
    std::vector<float> v[3];
    for (const auto &e : edges) {
        const Target t = target(g, ix, e, zero, gate);
        if (t.ok) v[kindIndex(e.kind)].push_back(t.signedOut);
    }
    for (int k = 0; k < 3; k++) {
        if (v[k].size() < 10) continue;
        std::nth_element(v[k].begin(), v[k].begin() + v[k].size() / 2, v[k].end());
        off[k] = std::clamp(v[k][v[k].size() / 2], -3.f, 3.f);
    }
}

double kindWeight(char k) { return k == 'K' ? 0.5 : 1.0; } // crossings are exact moments, riding a kerb is not

// Merges the edge points per 5 m of lap, side and kind into one consensus point each: the median position of every
// car and lap that left a point there (at least 3). `spread` gets the rms distance of the raw points from their
// consensus point - how consistent the data is with itself.
std::vector<CalEdge> consensus(const std::vector<CalEdge> &edges, float *spread)
{
    std::map<std::tuple<int, char, char>, std::vector<const CalEdge *>> groups;
    for (const auto &e : edges) groups[{int(e.dist / CAL_BIN_M), e.side, e.kind}].push_back(&e);
    std::vector<CalEdge> out;
    double s = 0;
    size_t n = 0;
    for (auto &[key, v] : groups) {
        if (v.size() < 3) continue;
        std::vector<float> xs, zs, ds;
        for (const CalEdge *e : v) { xs.push_back(e->x); zs.push_back(e->z); ds.push_back(e->dist); }
        auto median = [](std::vector<float> &a) {
            std::nth_element(a.begin(), a.begin() + a.size() / 2, a.end());
            return a[a.size() / 2];
        };
        CalEdge m;
        m.x = median(xs);
        m.z = median(zs);
        m.dist = median(ds);
        m.side = std::get<1>(key);
        m.kind = std::get<2>(key);
        for (const CalEdge *e : v) {
            const double d = std::hypot(e->x - m.x, e->z - m.z);
            s += d * d;
            n++;
        }
        out.push_back(m);
    }
    if (spread) *spread = n ? float(std::sqrt(s / double(n))) : 0.f;
    return out;
}

} // namespace

namespace {
Geometry fitOutline(const CalData &d, CalReport *report)
{
    CalReport r;
    Geometry g;
    r.coverage = d.coverage();
    const Shape *shape = forGame(d.trackId);
    if (!shape) {
        r.message = "这条赛道（编号 " + std::to_string(d.trackId) + "）没有内置赛道数据";
        if (report) *report = r;
        return g;
    }
    // 1. the averaged line of the cars on track -> first alignment
    std::vector<DrivenPoint> pts;
    for (size_t i = 0; i < d.bins.size(); i++)
        if (d.bins[i].n >= 2)
            pts.push_back({(float(i) + 0.5f) * CAL_BIN_M, float(d.bins[i].sx / d.bins[i].n), float(d.bins[i].sz / d.bins[i].n)});
    g = align(*shape, pts, d.trackLength, 40.f);
    if (!g.ok) {
        r.message = pts.size() < 50 ? "位置数据太少（覆盖不到赛道的四分之一）" : "内置赛道和录像里的行车线对不上（布局可能不同）";
        if (report) *report = r;
        g.ok = false;
        return g;
    }
    r.startRms = g.rms;
    DistIndex ix(g, d.trackLength);
    // 2. the edge points of all cars and laps merged into consensus points; the fit uses those, so every stretch of
    //    track counts once and stray points cannot pull it
    const std::vector<CalEdge> cons = consensus(d.edges, &r.consistency);
    r.consensusPoints = int(cons.size());
    float off[3] = {0, 0, 0}; // outer crossings, inner kerb edges, kerb riding: distance outside the outline's edge
    estimateOffsets(g, ix, cons, 12.f, off);
    r.kerbBefore = kerbRms(g, ix, cons, off, 12.f);

    // 2. refit the whole outline: kerb points onto the nearest edge (weight 1). The averaged line is a racing line,
    //    not the centre, and it is what the first alignment already used: with enough kerbs it only steadies the fit
    //    (5 % of the total weight), without kerbs it carries it.
    float maxD = 12.f;
    for (int it = 0; it < 30; it++) {
        std::vector<Pair> ps;
        estimateOffsets(g, ix, cons, maxD, off); // each kind of point keeps its own distance from the edge
        for (const auto &e : cons) {
            const Target t = target(g, ix, e, off, maxD);
            if (t.ok) ps.push_back({t.mx, t.mz, e.x, e.z, kindWeight(e.kind)});
        }
        r.kerbPoints = int(ps.size());
        const double lineW = ps.size() >= 50 ? 0.05 * double(ps.size()) / double(std::max<size_t>(1, pts.size())) : 0.2;
        for (const auto &p : pts) {
            const int i = ix.at(p.dist);
            ps.push_back({g.cx[i], g.cz[i], p.x, p.z, lineW});
        }
        const Sim t = solve(ps);
        applyAll(g, t);
        r.iterations = it + 1;
        const double moved = std::hypot(t.tx, t.tz) + std::fabs(t.s) * 1000 + std::fabs(t.c - 1) * 1000;
        maxD = std::max(4.f, maxD * 0.8f); // tighten the gate as it converges
        if (moved < 0.02) break;
    }
    estimateOffsets(g, ix, cons, 6.f, off);
    r.kerbGlobal = kerbRms(g, ix, cons, off, 6.f);

    // 3. local: per 50 m of lap, the mean offset of the kerb points from their edge, smoothed, at most 3 m
    const int segs = std::max(1, int(d.trackLength / 50.f));
    std::vector<double> ox(segs, 0), oz(segs, 0), on(segs, 0);
    for (const auto &e : cons) {
        const Target t = target(g, ix, e, off, 4.f);
        if (!t.ok) continue;
        const int s = std::clamp(int(e.dist / d.trackLength * segs), 0, segs - 1);
        const double w = kindWeight(e.kind);
        ox[s] += w * (e.x - t.mx);
        oz[s] += w * (e.z - t.mz);
        on[s] += w;
    }
    std::vector<float> cx(segs, 0), cz(segs, 0);
    for (int s = 0; s < segs; s++) {
        double sx = 0, sz = 0, sn = 0; // with half weight from the neighbours
        for (int k = -1; k <= 1; k++) {
            const int j = (s + k + segs) % segs;
            const double w = k ? 0.5 : 1.0;
            sx += w * ox[j];
            sz += w * oz[j];
            sn += w * on[j];
        }
        if (sn < 5) continue; // too few kerb points here: leave it
        float vx = float(sx / sn), vz = float(sz / sn);
        const float l = std::hypot(vx, vz);
        if (l > 3.f) { vx *= 3.f / l; vz *= 3.f / l; }
        cx[s] = vx;
        cz[s] = vz;
        r.segmentsCorrected++;
    }
    for (size_t i = 0; i < g.cx.size(); i++) { // interpolate between segment centres
        const float f = g.dist[i] / d.trackLength * float(segs) - 0.5f;
        const int a = int(std::floor(f));
        const float k = f - float(a);
        const int s0 = (a % segs + segs) % segs, s1 = (s0 + 1) % segs;
        const float dx = cx[s0] + (cx[s1] - cx[s0]) * k, dz = cz[s0] + (cz[s1] - cz[s0]) * k;
        g.cx[i] += dx; g.cz[i] += dz;
        g.lx[i] += dx; g.lz[i] += dz;
        g.rx[i] += dx; g.rz[i] += dz;
    }
    r.kerbLocal = kerbRms(g, ix, cons, off, 6.f);
    r.offOuter = off[0];
    r.offInner = off[1];
    r.offRide = off[2];
    for (const auto &e : d.edges) (e.kind == 'O' ? r.outerPoints : e.kind == 'I' ? r.innerPoints : r.ridePoints)++;
    r.ok = true;
    char b[512];
    std::snprintf(b, sizeof b,
                  "完成：行车线初始误差 %.1f m；边缘点 %d 个（赛道外沿 %d、路肩内沿 %d、压路肩 %d）合并成 %d 个共识点，"
                  "数据自身一致性 %.2f m；共识点到地图边缘 %.2f → %.2f → %.2f m（%d 段局部修正）；"
                  "各类点在地图边缘外 %+.2f / %+.2f / %+.2f m",
                  r.startRms, r.outerPoints + r.innerPoints + r.ridePoints, r.outerPoints, r.innerPoints, r.ridePoints,
                  r.consensusPoints, r.consistency, r.kerbBefore, r.kerbGlobal, r.kerbLocal, r.segmentsCorrected,
                  r.offOuter, r.offInner, r.offRide);
    r.message = b;
    if (report) *report = r;
    return g;
}

} // namespace

// ---- the track from the recorded data alone ------------------------------------------------------------------------
namespace {

constexpr float HALF_CAR = 1.0f; // a car's centre is this far inside the edge when its wheels are on it

// closed-loop moving average with weights 1 2 3 2 1
void smoothLoop(std::vector<float> &v, int passes)
{
    const int n = int(v.size());
    if (n < 5) return;
    for (int p = 0; p < passes; p++) {
        std::vector<float> o(n);
        for (int i = 0; i < n; i++) {
            float s = 0;
            for (int k = -2; k <= 2; k++) s += float(3 - std::abs(k)) * v[(i + k + n) % n];
            o[i] = s / 9.f;
        }
        v.swap(o);
    }
}

// Fills the entries that are not `have` by straight interpolation between their known neighbours (closed loop).
bool fillLoop(std::vector<float> &v, const std::vector<bool> &have)
{
    const int n = int(v.size());
    int first = -1;
    for (int i = 0; i < n; i++)
        if (have[i]) { first = i; break; }
    if (first < 0) return false;
    for (int i = 0; i < n; i++) {
        if (have[i]) continue;
        int a = i, b = i, da = 0, db = 0;
        while (!have[a]) { a = (a - 1 + n) % n; da++; }
        while (!have[b]) { b = (b + 1) % n; db++; }
        v[i] = v[a] + (v[b] - v[a]) * float(da) / float(da + db);
    }
    return true;
}

} // namespace

Geometry buildFromData(const CalData &d, std::string *why)
{
    Geometry g;
    const size_t nb = d.bins.size();
    auto fail = [&](const std::string &m) { if (why) *why = m; return Geometry{}; };
    if (nb < 50) return fail("赛道太短或没有场次信息");
    size_t withPos = 0;
    for (size_t i = 0; i < d.pos.size() && i < nb; i++) withPos += d.pos[i].size() >= 20;
    if (withPos == 0) return fail("数据文件里没有车辆位置样本（用新版本重新提取）");
    if (withPos < nb * 9 / 10)
        return fail("车辆位置只覆盖赛道的 " + std::to_string(withPos * 100 / nb) + "%（至少要 90%）");

    // 1. the averaged line, gaps filled, smoothed
    std::vector<float> cx(nb, 0), cz(nb, 0);
    std::vector<bool> have(nb, false);
    for (size_t i = 0; i < nb; i++)
        if (d.bins[i].n >= 2) {
            cx[i] = float(d.bins[i].sx / d.bins[i].n);
            cz[i] = float(d.bins[i].sz / d.bins[i].n);
            have[i] = true;
        }
    fillLoop(cx, have);
    fillLoop(cz, have);
    smoothLoop(cx, 2);
    smoothLoop(cz, 2);
    std::vector<float> nx(nb), nz(nb); // a normal to the line, the same side all the way round
    for (size_t i = 0; i < nb; i++) {
        const size_t a = (i + nb - 1) % nb, b = (i + 1) % nb;
        float tx = cx[b] - cx[a], tz = cz[b] - cz[a];
        const float tl = std::max(1e-4f, std::hypot(tx, tz));
        tx /= tl;
        tz /= tl;
        nx[i] = -tz;
        nz[i] = tx;
    }

    // 2. where the cars drive across the track: the 2nd and 98th percentile of their offsets from the line, plus
    //    half a car
    std::vector<float> uL(nb, 0), uR(nb, 0);
    std::vector<bool> haveW(nb, false);
    for (size_t i = 0; i < nb && i < d.pos.size(); i++) {
        if (d.pos[i].size() < 20) continue;
        std::vector<float> u;
        for (const auto &p : d.pos[i]) u.push_back((p[0] - cx[i]) * nx[i] + (p[1] - cz[i]) * nz[i]);
        std::sort(u.begin(), u.end());
        const float lo = u[u.size() * 2 / 100], hi = u[u.size() * 98 / 100];
        uL[i] = hi + HALF_CAR;
        uR[i] = lo - HALF_CAR;
        haveW[i] = true;
    }
    // 3. where a wheel crossed onto grass / gravel, that is the edge itself (consensus of all cars and laps)
    float spread = 0;
    int crossings = 0;
    for (const auto &e : consensus(d.edges, &spread)) {
        if (e.kind != 'O') continue;
        const size_t i = std::min(nb - 1, size_t(std::max(0.f, e.dist) / CAL_BIN_M));
        const float u = (e.x - cx[i]) * nx[i] + (e.z - cz[i]) * nz[i];
        if (std::fabs(u) > 25.f) continue;
        if (u > 0) uL[i] = u;
        else uR[i] = u;
        haveW[i] = true;
        crossings++;
    }
    fillLoop(uL, haveW);
    fillLoop(uR, haveW);
    smoothLoop(uL, 1);
    smoothLoop(uR, 1);
    for (size_t i = 0; i < nb; i++) { // at least 8 m wide
        const float w = uL[i] - uR[i];
        if (w < 8.f) { const float m = (uL[i] + uR[i]) / 2; uL[i] = m + 4.f; uR[i] = m - 4.f; }
    }

    // 4. the map: centre between the edges
    for (size_t i = 0; i < nb; i++) {
        const float mid = (uL[i] + uR[i]) / 2;
        g.dist.push_back((float(i) + 0.5f) * CAL_BIN_M);
        g.cx.push_back(cx[i] + nx[i] * mid);
        g.cz.push_back(cz[i] + nz[i] * mid);
        g.lx.push_back(cx[i] + nx[i] * uL[i]);
        g.lz.push_back(cz[i] + nz[i] * uL[i]);
        g.rx.push_back(cx[i] + nx[i] * uR[i]);
        g.rz.push_back(cz[i] + nz[i] * uR[i]);
    }
    const Shape *sh = forGame(d.trackId);
    g.name = sh ? sh->name : asciiName(d.trackId);
    g.source = "录像数据";
    g.ok = true;
    if (why) *why = "用了 " + std::to_string(crossings) + " 个赛道外沿共识点";
    return g;
}

namespace {
// How well a map agrees with the measured edges: consensus points vs. its edges, rms, each kind less its own offset.
float scoreMap(const Geometry &g, const std::vector<CalEdge> &cons, float length)
{
    if (!g.ok || cons.empty()) return 1e9f;
    DistIndex ix(g, length);
    float off[3] = {0, 0, 0};
    estimateOffsets(g, ix, cons, 6.f, off);
    return kerbRms(g, ix, cons, off, 6.f);
}
} // namespace

namespace {
// The surface scan as map cells: the class found most often in each cell (at least 2 hits).
void attachSurface(const CalData &d, Geometry &g)
{
    g.cell = CAL_CELL_M;
    g.surface.clear();
    for (const auto &[key, v] : d.grid) {
        int best = 0, total = 0;
        for (int k = 0; k < S_CLASSES; k++) {
            total += v[k];
            if (v[k] > v[best]) best = k;
        }
        if (total < 2) continue;
        g.surface.push_back({int32_t(uint32_t(key >> 32)), int32_t(uint32_t(key)), uint8_t(best)});
    }
    buildSurfaceRuns(g);
}

// Fits the edges and kerbs of a scanned map and completes it. The rule: find the switch points, sort them by kind,
// fit a curve to each kind, then fill between the curves.
//   1. Switch points, per centre point and side: five cross-section lines (1 m apart along the track) walk outward
//      from the centre over the scanned cells and note where the ground changes, up to the first off-track cell
//      (grass, gravel, water), which ends the track:
//        tarmac -> kerb  the kerb's inner side
//        kerb -> tarmac  the kerb's outer side, tarmac beyond it
//        kerb / tarmac -> off  the track's edge
//      plus the wheels' own crossings: tarmac <-> kerb ('I', of all recordings; the inner or outer side by where
//      they lie in the kerb) and track <-> grass / gravel (consensus 'O' points, the edge).
//   2. A curve per kind: at each point a straight line through that kind's points within 20 m either way, again
//      without those more than 0.75 m off, taken at the point.
//   3. The edge is the off-track curve. Where there is none (a wall, a side nobody drove on): the farthest scanned
//      track plus half a tyre where that lies beyond the map's edge; gaps up to 200 m bridged straight, longer ones
//      keep the map's own width; a side with no boundary within 100 m is at least as wide as the widest there.
//   4. A kerb is where a point has at least 3 kerb cells (gaps up to 15 m bridged unless tarmac was scanned there).
//      It lies between its inner curve and its outer side: the kerb -> tarmac curve, else the edge. Only a side
//      without a curve is placed by the kerb's fixed width (the spread of its cells, 0.8 - 2.5 m) from the other.
//   5. The regions between the curves (Geometry::bands): per 5 m and side, from the centre line to 3 m beyond
//      the edge, each with the class its place gives it unless the scan inside clearly says otherwise. These are
//      the map; the cells are completed too (tarmac between the edges, the ground beside them) for older readers.
// Returns the number of centre points with an off-track curve (both sides counted); `filled` gets the inferred cells.
int completeSurface(Geometry &g, size_t *filled, const std::vector<CalEdge> *edges, const std::vector<CalEdge> *raw)
{
    const size_t n = g.cx.size();
    if (filled) *filled = 0;
    if (n < 10 || g.surface.empty()) return 0;
    const float cell = g.cell;
    auto key = [](int32_t x, int32_t z) { return (uint64_t(uint32_t(x)) << 32) | uint32_t(z); };
    std::unordered_map<uint64_t, uint8_t> at;
    at.reserve(g.surface.size() * 2);
    for (const auto &c : g.surface) at[key(c.x, c.z)] = c.cls;
    auto isOff = [](int c) { return c == S_GRASS || c == S_GRAVEL || c == S_WATER; };
    auto classAt = [&](float x, float z) -> int {
        auto it = at.find(key(int32_t(std::floor(x / cell)), int32_t(std::floor(z / cell))));
        return it == at.end() ? -1 : it->second;
    };
    auto wrap = [n](long i) { return size_t(((i % long(n)) + long(n)) % long(n)); };

    // the centre line's frame: a normal per point, pointing to the left edge, and a coarse index to find the
    // nearest point
    std::vector<float> nx(n), nz(n), tx(n), tz(n), w0[2] = {std::vector<float>(n), std::vector<float>(n)};
    for (size_t i = 0; i < n; i++) {
        const size_t a = wrap(long(i) - 1), b = wrap(long(i) + 1);
        float x = g.cx[b] - g.cx[a], z = g.cz[b] - g.cz[a];
        const float l = std::max(1e-4f, std::hypot(x, z));
        tx[i] = x / l;
        tz[i] = z / l;
        nx[i] = -tz[i];
        nz[i] = tx[i];
        if (nx[i] * (g.lx[i] - g.cx[i]) + nz[i] * (g.lz[i] - g.cz[i]) < 0) { nx[i] = -nx[i]; nz[i] = -nz[i]; }
        w0[0][i] = std::hypot(g.lx[i] - g.cx[i], g.lz[i] - g.cz[i]);
        w0[1][i] = std::hypot(g.rx[i] - g.cx[i], g.rz[i] - g.cz[i]);
    }
    constexpr float HASH = 20.f;
    std::unordered_map<uint64_t, std::vector<uint32_t>> near;
    for (size_t i = 0; i < n; i++)
        near[key(int32_t(std::floor(g.cx[i] / HASH)), int32_t(std::floor(g.cz[i] / HASH)))].push_back(uint32_t(i));
    // the centre point a world point belongs to (within 4 m along the line) and its offset across, left > 0
    auto locate = [&](float x, float z, int &best, float &u) {
        best = -1;
        float bd = 1e9f;
        const int32_t hx = int32_t(std::floor(x / HASH)), hz = int32_t(std::floor(z / HASH));
        for (int dx = -1; dx <= 1; dx++)
            for (int dz = -1; dz <= 1; dz++) {
                auto it = near.find(key(hx + dx, hz + dz));
                if (it == near.end()) continue;
                for (uint32_t i : it->second) {
                    const float d = (x - g.cx[i]) * (x - g.cx[i]) + (z - g.cz[i]) * (z - g.cz[i]);
                    if (d < bd) { bd = d; best = int(i); }
                }
            }
        if (best < 0) return;
        const float px = x - g.cx[best], pz = z - g.cz[best];
        if (std::fabs(px * tx[best] + pz * tz[best]) > 4.f) { best = -1; return; }
        u = px * nx[best] + pz * nz[best];
    };

    // 1. switch points, as offsets from the centre line (always positive, per side)
    constexpr float STEP = 0.25f, REACH = 8.f, GAP = 1.f;
    struct Side {
        std::vector<std::vector<float>> ki, kt, off, kerb, far; // kerb inner, kerb -> tarmac, edge, kerb cells, track
        std::vector<int> outCls;                                // off-track class right beyond the edge
    } S[2];
    for (auto &s : S) {
        s.ki.assign(n, {});
        s.kt.assign(n, {});
        s.off.assign(n, {});
        s.kerb.assign(n, {});
        s.far.assign(n, {});
        s.outCls.assign(n, -1);
    }
    for (int side = 0; side < 2; side++) {
        const float sg = side ? -1.f : 1.f;
        auto &s = S[side];
        for (size_t i = 0; i < n; i++) {
            int cnt[S_CLASSES] = {};
            for (float along : {-2.f, -1.f, 0.f, 1.f, 2.f}) {
                int prev = -1;
                float prevU = 0, lastKnown = 0;
                for (float u = 0; u <= w0[side][i] + REACH; u += STEP) {
                    const int c = classAt(g.cx[i] + tx[i] * along + nx[i] * u * sg, g.cz[i] + tz[i] * along + nz[i] * u * sg);
                    if (c < 0) {
                        if (u - lastKnown > GAP) prev = -1; // too long unscanned: no switch across it
                        continue;
                    }
                    const float mid = (prevU + u) / 2;
                    if (isOff(c)) {
                        if (prev == S_TARMAC || prev == S_KERB) s.off[i].push_back(mid);
                        // the ground right beyond the edge
                        for (float v = u; v <= u + 1.5f; v += STEP) {
                            const int o = classAt(g.cx[i] + tx[i] * along + nx[i] * v * sg, g.cz[i] + tz[i] * along + nz[i] * v * sg);
                            if (o >= 0 && isOff(o)) cnt[o]++;
                        }
                        break; // the first off-track cell ends the track
                    }
                    if (c == S_KERB) s.kerb[i].push_back(u);
                    if (c == S_TARMAC || c == S_KERB) s.far[i].push_back(u);
                    if (prev == S_TARMAC && c == S_KERB) s.ki[i].push_back(mid);
                    if (prev == S_KERB && c == S_TARMAC) s.kt[i].push_back(mid);
                    if (c == S_TARMAC || c == S_KERB) prev = c;
                    prevU = u;
                    lastKnown = u;
                }
            }
            int best = -1;
            for (int c : {int(S_GRASS), int(S_GRAVEL), int(S_WATER)})
                if (cnt[c] >= 2 && (best < 0 || cnt[c] > cnt[best])) best = c;
            s.outCls[i] = best;
        }
    }
    // the wheels' crossings: tarmac <-> kerb by where they lie in the kerb, track <-> off as the edge
    g.switches.clear();
    if (raw)
        for (const auto &e : *raw) {
            if (e.kind != 'I') continue;
            g.switches.push_back({e.x, e.z});
        }
    {
        std::vector<std::vector<float>> wheelIn[2] = {std::vector<std::vector<float>>(n), std::vector<std::vector<float>>(n)};
        std::vector<std::vector<float>> wheelOut[2] = {std::vector<std::vector<float>>(n), std::vector<std::vector<float>>(n)};
        if (raw)
            for (const auto &e : *raw) {
                if (e.kind != 'I') continue;
                int i;
                float u = 0;
                locate(e.x, e.z, i, u);
                if (i < 0) continue;
                const int side = u > 0 ? 0 : 1;
                auto &k = S[side].kerb[size_t(i)];
                if (k.size() < 3) continue;
                std::vector<float> v = k;
                std::nth_element(v.begin(), v.begin() + v.size() / 2, v.end());
                (std::fabs(u) < v[v.size() / 2] ? wheelIn : wheelOut)[side][size_t(i)].push_back(std::fabs(u));
            }
        // the wheels' points replace the cells' along a stretch where they are plentiful (the cells blur by half a
        // cell)
        for (int side = 0; side < 2; side++)
            for (size_t i = 0; i < n; i++) {
                size_t wi = 0, wo = 0;
                for (long k = -4; k <= 4; k++) { wi += wheelIn[side][wrap(long(i) + k)].size(); wo += wheelOut[side][wrap(long(i) + k)].size(); }
                if (wi >= 3) S[side].ki[i] = wheelIn[side][i];
                if (wo >= 3) S[side].kt[i] = wheelOut[side][i];
            }
        if (edges)
            for (const auto &e : *edges) {
                if (e.kind != 'O') continue;
                int i;
                float u = 0;
                locate(e.x, e.z, i, u);
                if (i >= 0 && std::fabs(u) > 2.f) S[u > 0 ? 0 : 1].off[size_t(i)].push_back(std::fabs(u));
            }
    }

    // 2. a curve per kind: a straight line through the points within 20 m, outliers left out, taken at the point
    auto fitCurve = [&](const std::vector<std::vector<float>> &pts, std::vector<float> &v) {
        v.assign(n, NAN);
        for (size_t i = 0; i < n; i++) {
            std::vector<std::array<float, 2>> q;
            for (long k = -4; k <= 4; k++)
                for (float u : pts[wrap(long(i) + k)]) q.push_back({float(k), u});
            const size_t here = pts[i].size() + pts[wrap(long(i) - 1)].size() + pts[wrap(long(i) + 1)].size();
            if (q.size() < 4 || here < 1) continue;
            // start from the median (robust), then least squares twice
            std::vector<float> us;
            for (const auto &p : q) us.push_back(p[1]);
            std::nth_element(us.begin(), us.begin() + us.size() / 2, us.end());
            float a = us[us.size() / 2], b = 0;
            bool ok = true;
            for (int pass = 0; pass < 2 && ok; pass++) {
                double sk = 0, su = 0, skk = 0, sku = 0, c = 0;
                for (const auto &p : q) {
                    if (std::fabs(p[1] - (a + b * p[0])) > (pass ? 0.75f : 1.5f)) continue;
                    sk += p[0]; su += p[1]; skk += p[0] * p[0]; sku += p[0] * p[1]; c++;
                }
                if (c < 3) { ok = false; break; }
                const double det = c * skk - sk * sk;
                b = std::fabs(det) < 1e-6 ? 0.f : float((c * sku - sk * su) / det);
                a = float((su - b * sk) / c);
            }
            if (ok) v[i] = a;
        }
    };
    std::vector<float> fOff[2], fKi[2], fKt[2];
    for (int side = 0; side < 2; side++) {
        fitCurve(S[side].off, fOff[side]);
        fitCurve(S[side].ki, fKi[side]);
        fitCurve(S[side].kt, fKt[side]);
    }

    // 3. the edges
    std::vector<float> W[2];
    int moved = 0;
    for (int side = 0; side < 2; side++) {
        auto &w = W[side];
        w.assign(n, 0);
        std::vector<bool> have(n, false);
        for (size_t i = 0; i < n; i++) {
            if (!std::isnan(fOff[side][i])) {
                w[i] = fOff[side][i];
                have[i] = true;
                moved++;
            } else if (auto f = S[side].far[i]; f.size() >= 10) { // no boundary: the farthest scanned track
                std::nth_element(f.begin(), f.begin() + f.size() * 98 / 100, f.end());
                const float far = f[f.size() * 98 / 100] + 0.45f;
                if (far >= w0[side][i]) { w[i] = far; have[i] = true; }
            }
        }
        if (std::count(have.begin(), have.end(), true) < long(n / 20)) { w = w0[side]; continue; }
        // gaps up to 200 m bridged straight; longer ones keep the map's own width (blended over 40 m)
        std::vector<float> filledW(w);
        fillLoop(filledW, have);
        std::vector<int> dist(n, 1 << 20);
        for (int pass = 0; pass < 2; pass++)
            for (size_t k = 0; k < 2 * n; k++) {
                const size_t i = pass ? (2 * n - 1 - k) % n : k % n, p = pass ? (i + 1) % n : wrap(long(i) - 1);
                dist[i] = have[i] ? 0 : std::min(dist[i], dist[p] + 1);
            }
        for (size_t i = 0; i < n; i++) {
            const float t = std::clamp(1.f - float(dist[i] - 20) / 8.f, 0.f, 1.f);
            w[i] = filledW[i] * t + w0[side][i] * (1 - t);
        }
        smoothLoop(w, 2);
        // a side with no boundary within 100 m: at least as wide as the widest within 100 m (nobody drove wider)
        std::vector<float> wide(n);
        std::vector<bool> open(n);
        for (size_t i = 0; i < n; i++) {
            float mx = w[i];
            bool any = false;
            for (long k = -20; k <= 20; k++) {
                mx = std::max(mx, w[wrap(long(i) + k)]);
                any |= !std::isnan(fOff[side][wrap(long(i) + k)]);
            }
            wide[i] = mx;
            open[i] = !any;
        }
        smoothLoop(wide, 3);
        for (size_t i = 0; i < n; i++)
            if (open[i]) w[i] = std::max(w[i], std::min(wide[i], w[i] + 6.f));
        for (size_t i = 0; i < n; i++) w[i] = std::clamp(w[i], 3.f, 25.f);
    }

    // 4. the kerbs
    constexpr float KERB_MIN = 0.8f, KERB_MAX = 2.5f;
    g.kl.assign(n, 0.f);
    g.kr.assign(n, 0.f);
    g.klo.assign(n, 0.f);
    g.kro.assign(n, 0.f);
    auto pct = [](std::vector<float> v, int p) {
        std::nth_element(v.begin(), v.begin() + v.size() * size_t(p) / 100, v.end());
        return v[v.size() * size_t(p) / 100];
    };
    for (int side = 0; side < 2; side++) {
        const float sg = side ? -1.f : 1.f;
        auto &s = S[side];
        const auto &w = W[side];
        std::vector<bool> has(n);
        for (size_t i = 0; i < n; i++) has[i] = s.kerb[i].size() >= 3;
        // scanned tarmac where a kerb would be: then a gap is not bridged
        std::vector<bool> tarmac(n, false);
        for (size_t i = 0; i < n; i++) {
            int t = 0;
            for (float u = w[i] - 1.2f; u <= w[i] - 0.3f; u += STEP)
                t += classAt(g.cx[i] + nx[i] * u * sg, g.cz[i] + nz[i] * u * sg) == S_TARMAC;
            tarmac[i] = t >= 2;
        }
        std::vector<bool> has2 = has;
        for (size_t i = 0; i < n; i++) {
            if (has[i] || tarmac[i]) continue;
            int a = 1, b = 1;
            while (a <= 3 && !has[wrap(long(i) - a)]) a++;
            while (b <= 3 && !has[wrap(long(i) + b)]) b++;
            if (a > 3 || b > 3 || a + b > 5) continue;
            bool clear = true;
            for (int d = -(a - 1); d <= b - 1 && clear; d++) clear = !tarmac[wrap(long(i) + d)];
            if (clear) has2[i] = true;
        }
        size_t start = 0;
        while (start < n && has2[start]) start++; // a point without kerb, so runs do not wrap mid-way
        if (start == n) start = 0;
        for (size_t k = 0; k < n;) {
            const size_t i0 = (start + k) % n;
            if (!has2[i0]) { k++; continue; }
            size_t len = 0;
            while (len < n && has2[(i0 + len) % n]) len++;
            k += len;
            // the fixed width, for a side without a curve: the spread of the kerb cells
            std::vector<float> spread;
            for (size_t m = 0; m < len; m++)
                if (const auto &v = s.kerb[(i0 + m) % n]; v.size() >= 3) spread.push_back(pct(v, 90) - pct(v, 10) + cell);
            const float width = spread.empty() ? KERB_MIN : std::clamp(pct(spread, 50), KERB_MIN, KERB_MAX);
            for (size_t m = 0; m < len; m++) {
                const size_t i = (i0 + m) % n;
                float in = fKi[side][i], out = fKt[side][i];
                if (!std::isnan(out) && out > w[i]) out = NAN; // a kerb -> tarmac switch beyond the edge: none
                if (std::isnan(out)) out = w[i];               // the kerb runs out to the edge
                if (std::isnan(in)) in = out - width;          // no inner curve: the fixed width
                // never over tarmac the scan found inside the kerb: at most 0.3 m inside its innermost cells
                if (const auto &kc = s.kerb[i]; kc.size() >= 3) in = std::max(in, pct(kc, 5) - 0.3f);
                in = std::clamp(in, out - 5.f, out - 0.3f);
                (side ? g.kr : g.kl)[i] = w[i] - in;
                (side ? g.kro : g.klo)[i] = w[i] - out;
            }
        }
        // the off-track class beside the edge, else from the nearest point within 30 m
        std::vector<int> o2 = s.outCls;
        for (size_t i = 0; i < n; i++)
            for (long d = 1; d <= 6 && o2[i] < 0; d++) {
                if (s.outCls[wrap(long(i) - d)] >= 0) o2[i] = s.outCls[wrap(long(i) - d)];
                else if (s.outCls[wrap(long(i) + d)] >= 0) o2[i] = s.outCls[wrap(long(i) + d)];
            }
        s.outCls = o2;
    }
    for (size_t i = 0; i < n; i++) {
        g.lx[i] = g.cx[i] + nx[i] * W[0][i];
        g.lz[i] = g.cz[i] + nz[i] * W[0][i];
        g.rx[i] = g.cx[i] - nx[i] * W[1][i];
        g.rz[i] = g.cz[i] - nz[i] * W[1][i];
    }

    // 5. fill the unscanned cells: the off-track ground for 3 m beyond the edges, tarmac between them
    std::unordered_map<uint64_t, uint8_t> fill;
    auto put = [&](float x, float z, uint8_t cls) {
        const uint64_t k = key(int32_t(std::floor(x / cell)), int32_t(std::floor(z / cell)));
        if (!at.count(k)) fill.emplace(k, cls); // the first guess stays: the outside goes before tarmac
    };
    auto inferred = [](int cls) -> uint8_t {
        return cls == S_GRASS ? S_INF_GRASS : cls == S_GRAVEL ? S_INF_GRAVEL : S_INF_WATER;
    };
    constexpr float OUTSIDE = 3.f;
    // 5. the regions between the curves: per 5 m segment and side, bands from the centre line out to 3 m beyond the
    //    edge, bounded by the curves (kerb inner side, kerb outer side, edge). Each band has the class its place
    //    gives it (tarmac inside, kerb between the kerb curves, the off-track ground beyond the edge) unless the scan
    //    inside it clearly says otherwise (at least 8 samples, 70 % of one class, of the same family: tarmac / kerb
    //    inside the edge, grass / gravel / water beyond it).
    g.bands.clear();
    for (size_t i = 0; i < n; i++) {
        const size_t j = (i + 1) % n;
        if (std::hypot(g.cx[j] - g.cx[i], g.cz[j] - g.cz[i]) > 50.f) continue; // the seam of an open line
        for (int side = 0; side < 2; side++) {
            const float sg = side ? -1.f : 1.f;
            const auto &kw = side ? g.kr : g.kl, &ko = side ? g.kro : g.klo;
            // the boundaries at both ends, as offsets from the centre line, with the class of the band inside each
            struct Cut { float a, b; uint8_t cls; };
            std::vector<Cut> cuts;
            const float ea = W[side][i], eb = W[side][j];
            const bool kerb = kw[i] > 0 && kw[j] > 0;
            if (kerb) {
                cuts.push_back({ea - kw[i], eb - kw[j], S_TARMAC});
                if (ko[i] > 0.05f || ko[j] > 0.05f) cuts.push_back({ea - ko[i], eb - ko[j], S_KERB});
                cuts.push_back({ea, eb, uint8_t(ko[i] > 0.05f || ko[j] > 0.05f ? S_TARMAC : S_KERB)});
            } else {
                cuts.push_back({ea, eb, S_TARMAC});
            }
            const int oc = S[side].outCls[i] >= 0 ? S[side].outCls[i] : S[side].outCls[j];
            if (oc >= 0) cuts.push_back({ea + OUTSIDE, eb + OUTSIDE, uint8_t(oc)});
            float pa = 0, pb = 0;
            for (const auto &c : cuts) {
                // the scan inside the band
                int votes[S_CLASSES] = {}, total = 0;
                for (float t = 0.1f; t < 1.f; t += 0.2f)
                    for (float f = 0.05f; f < 1.f; f += 0.1f) {
                        const float u0 = pa + (pb - pa) * t, u1 = c.a + (c.b - c.a) * t, u = u0 + (u1 - u0) * f;
                        const float x = g.cx[i] + (g.cx[j] - g.cx[i]) * t + (nx[i] + (nx[j] - nx[i]) * t) * u * sg;
                        const float z = g.cz[i] + (g.cz[j] - g.cz[i]) * t + (nz[i] + (nz[j] - nz[i]) * t) * u * sg;
                        const int v = classAt(x, z);
                        if (v >= 0 && v < S_CLASSES) { votes[v]++; total++; }
                    }
                // only within its family: track bands between tarmac and kerb, the outside among off-track grounds
                uint8_t cls = c.cls;
                const bool trackBand = c.cls == S_TARMAC || c.cls == S_KERB;
                for (int v = 0; v < S_CLASSES; v++)
                    if (total >= 8 && votes[v] * 10 >= total * 7 && (trackBand ? v == S_TARMAC || v == S_KERB : isOff(v)))
                        cls = uint8_t(v);
                if (c.a > pa + 0.02f || c.b > pb + 0.02f) g.bands.push_back({uint32_t(i), uint8_t(side), pa, pb, c.a, c.b, cls});
                pa = c.a;
                pb = c.b;
            }
        }
    }

    // the cells too (for the data and for older programs)
    for (size_t i = 0; i < n; i++) {
        const size_t j = (i + 1) % n;
        const float seg = std::hypot(g.cx[j] - g.cx[i], g.cz[j] - g.cz[i]);
        if (seg > 50.f) continue; // the seam of an open line
        const int steps = std::max(1, int(seg / (cell * 0.5f)));
        for (int st = 0; st < steps; st++) {
            const float t = float(st) / float(steps);
            const size_t nearest = t < 0.5f ? i : j;
            const float x = g.cx[i] + (g.cx[j] - g.cx[i]) * t, z = g.cz[i] + (g.cz[j] - g.cz[i]) * t;
            float ux = nx[i] + (nx[j] - nx[i]) * t, uz = nz[i] + (nz[j] - nz[i]) * t;
            const float ul = std::max(1e-4f, std::hypot(ux, uz));
            ux /= ul;
            uz /= ul;
            const float e[2] = {W[0][i] + (W[0][j] - W[0][i]) * t, W[1][i] + (W[1][j] - W[1][i]) * t};
            for (int side = 0; side < 2; side++) {
                const float sg = side ? -1.f : 1.f;
                const int oc = S[side].outCls[nearest];
                if (oc < 0) continue;
                for (float u = e[side] + cell * 0.5f; u <= e[side] + OUTSIDE; u += cell * 0.5f) {
                    const float px = x + ux * u * sg, pz = z + uz * u * sg;
                    const int c = classAt(px, pz);
                    if (c >= 0 && isOff(c) && c != oc) break; // other scanned off-track ground
                    put(px, pz, inferred(oc));
                }
            }
            for (float u = -e[1]; u <= e[0]; u += cell * 0.5f) put(x + ux * u, z + uz * u, S_INFERRED);
        }
    }
    for (const auto &[k, cls] : fill) g.surface.push_back({int32_t(uint32_t(k >> 32)), int32_t(uint32_t(k)), cls});
    if (filled) *filled = fill.size();
    buildSurfaceRuns(g);
    return moved;
}

} // namespace

Geometry calibrate(const CalData &d, CalReport *report, CalMethod method)
{
    CalReport r, ro;
    Geometry og, dg;
    if (method != CalMethod::Data) og = fitOutline(d, &ro);
    std::string why;
    if (method != CalMethod::Outline) dg = buildFromData(d, &why);
    float spread = 0;
    const auto cons = consensus(d.edges, &spread);
    const float so = scoreMap(og, cons, d.trackLength), sd = scoreMap(dg, cons, d.trackLength);
    // the data map wins when it fits the measured edges clearly better (or the outline did not fit at all)
    const bool useData = dg.ok && (!og.ok || method == CalMethod::Data || sd < so - 0.1f);
    // the scan completes the chosen map: edges where the wheels found the boundary, tarmac between them
    auto finish = [&cons, &d](Geometry &g, CalReport &r) {
        size_t filled = 0;
        r.edgesFromScan = completeSurface(g, &filled, &cons, &d.edges);
        r.inferredCells = int(filled);
        if (r.edgesFromScan || filled) {
            char m[160];
            std::snprintf(m, sizeof m, "；按扫描拟合边缘 %d 处，推算补全路面 %zu 格", r.edgesFromScan, filled);
            r.message += m;
        }
    };
    r = ro;
    r.outlineScore = og.ok ? so : 0;
    r.dataScore = dg.ok ? sd : 0;
    r.consistency = spread;
    r.consensusPoints = int(cons.size());
    r.coverage = d.coverage();
    char b[300];
    if (useData) {
        r.ok = true;
        r.method = 'D';
        if (og.ok)
            std::snprintf(b, sizeof b, "按录像数据生成赛道（%s）：边缘点误差 %.2f m，内置赛道图配准后 %.2f m；数据自身一致性 %.2f m",
                          why.c_str(), sd, so, spread);
        else
            std::snprintf(b, sizeof b, "按录像数据生成赛道（%s；内置赛道图配不上：%s）：边缘点误差 %.2f m；数据自身一致性 %.2f m",
                          why.c_str(), ro.message.c_str(), sd, spread);
        r.message = b;
        attachSurface(d, dg);
        finish(dg, r);
        if (report) *report = r;
        if (d.gameYear) dg.season = 2000 + d.gameYear;
        return dg;
    }
    if (og.ok) {
        r.method = 'O';
        if (dg.ok) {
            std::snprintf(b, sizeof b, "；按录像数据生成的赛道误差 %.2f m，没有更好", sd);
            r.message += b;
        } else if (method != CalMethod::Outline && !why.empty()) {
            r.message += "；没有按录像数据生成赛道：" + why;
        }
        attachSurface(d, og);
        finish(og, r);
        if (report) *report = r;
        if (d.gameYear) og.season = 2000 + d.gameYear;
        return og;
    }
    if (method != CalMethod::Outline && !why.empty()) r.message += "；按录像数据生成也不行：" + why;
    if (report) *report = r;
    return og;
}

// ---- map files ----------------------------------------------------------------------------------------------------
//   f1race-trackmap 1 / track <id> / name <..> / info <..> / source <..> / p <dist> <cx> <cz> <lx> <lz> <rx> <rz>
bool saveDisplayMap(const Geometry &g, int trackId, const std::string &path, std::string *error)
{
    std::ofstream f(std::filesystem::path(path), std::ios::binary);
    if (!f) { if (error) *error = "写不了文件"; return false; }
    f << "f1race-trackmap 1\ntrack " << trackId << "\nname " << g.name << "\ndata 2D 简图\n";
    if (g.season) f << "season " << g.season << "\n";
    char b[160];
    auto at = [](const std::vector<float> &v, size_t i) { return i < v.size() ? v[i] : 0.f; };
    for (size_t i = 0; i < g.cx.size(); i++) {
        std::snprintf(b, sizeof b, "p %.2f %.2f %.2f %.2f %.2f %.2f %.2f %.2f %.2f %.2f %.2f\n", g.dist[i], g.cx[i], g.cz[i],
                      g.lx[i], g.lz[i], g.rx[i], g.rz[i], at(g.kl, i), at(g.kr, i), at(g.klo, i), at(g.kro, i));
        f << b;
    }
    for (const auto &r : g.bands) {
        std::snprintf(b, sizeof b, "b %u %u %.2f %.2f %.2f %.2f %u\n", r.i, unsigned(r.side), r.in0, r.in1, r.out0, r.out1,
                      unsigned(r.cls));
        f << b;
    }
    return bool(f);
}

bool saveMap(const Geometry &g, int trackId, const CalReport &r, const std::vector<std::string> &sources,
             const std::string &path, std::string *error, const CalData *data)
{
    std::filesystem::create_directories(std::filesystem::path(path).parent_path());
    std::ofstream f(std::filesystem::path(path), std::ios::binary);
    if (!f) { if (error) *error = "写不了文件"; return false; }
    f << "f1race-trackmap 1\ntrack " << trackId << "\nname " << g.name << "\ndata " << g.source << "\ninfo " << r.message
      << "\n";
    const int season = g.season ? g.season : data && data->gameYear ? 2000 + data->gameYear : 0;
    if (season) f << "season " << season << "\n";
    for (const auto &s : sources) f << "source " << s << "\n";
    char b[160];
    for (size_t i = 0; i < g.cx.size(); i++) {
        // the kerbs at the end (older readers ignore them): inner and outer side as distances inside each edge
        auto at = [](const std::vector<float> &v, size_t i) { return i < v.size() ? v[i] : 0.f; };
        std::snprintf(b, sizeof b, "p %.2f %.3f %.3f %.3f %.3f %.3f %.3f %.2f %.2f %.2f %.2f\n", g.dist[i], g.cx[i], g.cz[i],
                      g.lx[i], g.lz[i], g.rx[i], g.rz[i], at(g.kl, i), at(g.kr, i), at(g.klo, i), at(g.kro, i));
        f << b;
    }
    for (const auto &s : g.switches) {
        std::snprintf(b, sizeof b, "w %.2f %.2f\n", s[0], s[1]);
        f << b;
    }
    for (const auto &r : g.bands) { // b <point> <side 0 left / 1 right> <inner from, to> <outer from, to> <class>
        std::snprintf(b, sizeof b, "b %u %u %.2f %.2f %.2f %.2f %u\n", r.i, unsigned(r.side), r.in0, r.in1, r.out0, r.out1,
                      unsigned(r.cls));
        f << b;
    }
    if (!g.surface.empty()) f << "cell " << g.cell << "\n";
    for (const auto &s : g.surface) f << "s " << s.x << " " << s.z << " " << int(s.cls) << "\n";
    if (data) { // the scan the map was made from, so maps can be integrated later
        f << "data-begin\n";
        writeData(f, *data);
        f << "data-end\n";
    }
    return bool(f);
}

bool parseMap(const std::string &text, Geometry &g, int *trackId, std::string *info, CalData *data, bool *hasData)
{
    if (hasData) *hasData = false;
    std::istringstream f(text);
    std::string line;
    if (!std::getline(f, line) || line.rfind("f1race-trackmap", 0) != 0) return false;
    g = {};
    int id = -1;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::istringstream is(line);
        std::string k;
        is >> k;
        if (k == "track") is >> id;
        else if (k == "name") g.name = line.size() > 5 ? line.substr(5) : "";
        else if (k == "data") g.source = line.size() > 5 ? line.substr(5) : "";
        else if (k == "info" && info) *info = line.size() > 5 ? line.substr(5) : "";
        else if (k == "cell") is >> g.cell;
        else if (k == "season") is >> g.season;
        else if (k == "data-begin") {
            if (!data) { // skip it
                while (std::getline(f, line) && line.rfind("data-end", 0) != 0) {}
                if (hasData) *hasData = true;
                continue;
            }
            if (readData(f, *data, nullptr, "data-end") && hasData) *hasData = true;
        }
        else if (k == "b") {
            unsigned i, side, cls;
            float v[4];
            if (is >> i >> side >> v[0] >> v[1] >> v[2] >> v[3] >> cls && side < 2 && cls < S_CLASSES)
                g.bands.push_back({i, uint8_t(side), v[0], v[1], v[2], v[3], uint8_t(cls)});
        } else if (k == "w") {
            float x, z;
            if (is >> x >> z) g.switches.push_back({x, z});
        } else if (k == "s") {
            int32_t x, z;
            int cls;
            if (is >> x >> z >> cls && cls >= 0 && cls < S_ALL) g.surface.push_back({x, z, uint8_t(cls)});
        } else if (k == "p") {
            float v[7];
            if (is >> v[0] >> v[1] >> v[2] >> v[3] >> v[4] >> v[5] >> v[6]) {
                g.dist.push_back(v[0]);
                g.cx.push_back(v[1]); g.cz.push_back(v[2]);
                g.lx.push_back(v[3]); g.lz.push_back(v[4]);
                g.rx.push_back(v[5]); g.rz.push_back(v[6]);
                float k[4] = {0, 0, 0, 0};
                if (is >> k[0] >> k[1] >> k[2] >> k[3]) {}
                g.kl.push_back(k[0]);
                g.kr.push_back(k[1]);
                g.klo.push_back(k[2]);
                g.kro.push_back(k[3]);
            }
        }
    }
    if (trackId) *trackId = id;
    g.ok = g.cx.size() >= 8;
    g.rms = 0;
    buildSurfaceRuns(g);
    return g.ok;
}

bool loadMap(const std::string &path, Geometry &g, int *trackId, std::string *info, CalData *data, bool *hasData)
{
    std::ifstream f(std::filesystem::path(path), std::ios::binary);
    if (!f) return false;
    std::stringstream ss;
    ss << f.rdbuf();
    return parseMap(ss.str(), g, trackId, info, data, hasData);
}

} // namespace tracks
