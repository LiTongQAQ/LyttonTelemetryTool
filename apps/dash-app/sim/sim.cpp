#include "sim.hpp"

#include "tracks.hpp"

#include <f1tel/packets.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <mutex>
#include <random>
#include <string>
#include <vector>

namespace f1sim {

using namespace f1tel;

namespace {

constexpr int SIM_TRACK = 11; // Monza in the game's track ids; the outline comes from the bundled track data
float L = 5000.f;             // track length, m (set from the outline)
float S1_END = 1700.f, S2_END = 3400.f;
constexpr int CARS = 20;
constexpr int PLAYER = 4;
constexpr double TICK = 1.0 / 60.0;
constexpr float VMAX = 335.f / 3.6f;           // m/s
constexpr float A_BRAKE = 38.f, A_LAT = 36.f;  // m/s²

const char *NAMES[CARS] = {"VERSTAPPEN", "NORRIS",  "LECLERC", "PIASTRI",    "RUSSELL", "HAMILTON",  "ANTONELLI",
                           "ALONSO",     "SAINZ",   "GASLY",   "HULKENBERG", "TSUNODA", "ALBON",     "OCON",
                           "LAWSON",     "BEARMAN", "STROLL",  "HADJAR",     "COLAPINTO", "BORTOLETO"};
const uint8_t TEAMS[CARS] = {2, 8, 1, 8, 0, 1, 0, 4, 3, 5, 9, 2, 3, 7, 6, 7, 4, 6, 5, 9};
const uint8_t NUMBERS[CARS] = {1, 4, 16, 81, 63, 44, 12, 14, 55, 10, 27, 22, 23, 31, 30, 87, 18, 6, 43, 5};

// Speeds and inputs along a real circuit outline: cornering speed from the curvature, then braking and
// acceleration limits backwards and forwards around the lap.
struct Profile {
    std::vector<float> v, thr, brk, steer, curv; // per metre
    std::vector<float> x, z, heading;            // circuit geometry per metre (outline frame)
    Profile()
    {
        const tracks::Shape *sh = tracks::forGame(SIM_TRACK);
        const int m = int(sh->x.size());
        std::vector<float> cum(m + 1, 0.f);
        for (int i = 0; i < m; i++) {
            const int j = (i + 1) % m;
            cum[i + 1] = cum[i] + std::hypot(sh->x[j] - sh->x[i], sh->y[j] - sh->y[i]);
        }
        L = cum[m];
        S1_END = std::round(L * 0.33f);
        S2_END = std::round(L * 0.65f);
        const int n = int(L);
        x.resize(n);
        z.resize(n);
        for (int d = 0, seg = 0; d < n; d++) {
            const float s = L * float(d) / float(n);
            while (cum[seg + 1] < s) seg++;
            const int j = (seg + 1) % m;
            const float t = (s - cum[seg]) / std::max(1e-6f, cum[seg + 1] - cum[seg]);
            x[d] = sh->x[seg] + (sh->x[j] - sh->x[seg]) * t;
            z[d] = sh->y[seg] + (sh->y[j] - sh->y[seg]) * t;
        }
        auto wrap = [n](int i) { return ((i % n) + n) % n; };
        heading.resize(n);
        for (int d = 0; d < n; d++)
            heading[d] = std::atan2(z[wrap(d + 2)] - z[wrap(d - 2)], x[wrap(d + 2)] - x[wrap(d - 2)]);
        // curvature from the heading change over +-12 m (smooths the outline's noise)
        curv.resize(n);
        for (int d = 0; d < n; d++) {
            float dh = heading[wrap(d + 12)] - heading[wrap(d - 12)];
            while (dh > 3.14159265f) dh -= 6.2831853f;
            while (dh < -3.14159265f) dh += 6.2831853f;
            curv[d] = dh / 24.f;
        }
        v.resize(n);
        for (int d = 0; d < n; d++) v[d] = std::min(VMAX, std::sqrt(A_LAT / std::max(1e-5f, std::fabs(curv[d]))));
        for (int pass = 0; pass < 2; pass++) {
            for (int d = 2 * n - 1; d >= 0; d--) { // braking: reachable from the next metre
                const int i = wrap(d), j = wrap(d + 1);
                v[i] = std::min(v[i], std::sqrt(v[j] * v[j] + 2 * A_BRAKE));
            }
            for (int d = 0; d < 2 * n; d++) { // acceleration, weaker at speed
                const int i = wrap(d), j = wrap(d - 1);
                const float a = 13.f * (1.f - 0.75f * v[j] / VMAX);
                v[i] = std::min(v[i], std::sqrt(v[j] * v[j] + 2 * a));
            }
        }
        std::vector<float> t(n), b(n), s(n);
        for (int d = 0; d < n; d++) {
            const float v0 = v[d], v1 = v[wrap(d + 1)];
            const float acc = (v1 * v1 - v0 * v0) / 2; // m/s² per metre travelled
            if (acc < -0.5f) { b[d] = std::min(1.f, -acc / A_BRAKE * 1.1f); t[d] = 0; }
            else if (acc > 0.3f || v0 >= VMAX - 0.01f) { t[d] = 1; b[d] = 0; }
            else { t[d] = 0.35f; b[d] = 0; }
            s[d] = std::clamp(-curv[d] * 18.f, -1.f, 1.f); // + = right
        }
        auto smooth = [n, wrap](const std::vector<float> &in, int r) {
            std::vector<float> o(n);
            for (int d = 0; d < n; d++) {
                float sum = 0;
                for (int k = -r; k <= r; k++) sum += in[wrap(d + k)];
                o[d] = sum / float(2 * r + 1);
            }
            return o;
        };
        thr = smooth(t, 12);
        brk = smooth(b, 6);
        steer = smooth(s, 10);
    }
    template <typename T> static float get(const std::vector<T> &a, float d)
    {
        const int n = int(a.size());
        int i = int(std::floor(d));
        float k = d - float(i);
        i = ((i % n) + n) % n;
        return a[i] * (1 - k) + a[(i + 1) % n] * k;
    }
};

const Profile &profile()
{
    static Profile p;
    return p;
}

struct Lap {
    uint32_t ms = 0;
    uint32_t s[3] = {};
    bool valid = true;
};

struct Car {
    float pace = 1, lapPace = 1;
    double total = 0;
    double lapStart = 0, sectorStart = 0;
    uint32_t s1 = 0, s2 = 0;
    int sector = 0;
    bool invalid = false;
    float invalidAt = -1;
    std::vector<Lap> laps;
    uint8_t visualTyre = 17, tyreAge = 0, grid = 0;
    float speed = 0; // m/s
    float wear = 0;

    int lapNum() const { return int(total / L) + 1; }
    float lapDist() const { return float(std::fmod(total, double(L))); }
};

void putSplit(uint32_t ms, uint16_t &msPart, uint8_t &minPart)
{
    minPart = uint8_t(ms / 60000);
    msPart = uint16_t(ms % 60000);
}

} // namespace

struct Sim::Impl {
    std::mt19937 rng;
    uint64_t uid;
    double t = 0, acc = 0;
    uint32_t frame = 0;
    int historyCar = 0;
    std::array<Car, CARS> cars;
    std::vector<float> firstAt; // first time any car reached each 10 m bucket of total distance
    // player-only state
    float brakeTemp[4] = {450, 450, 500, 500};
    float tyreSurf[4] = {88, 88, 92, 92};
    float fuel = 62.f, ers = 3.0e6f;
    // power unit wear (%, carried over from earlier races), damage and scripted incidents
    float pu[6] = {21.f, 26.f, 18.f, 14.f, 11.f, 23.f}; // ICE, MGU-H, MGU-K, ES, CE, TC
    float gearbox = 7.f;
    float wingL = 0, wingR = 0, rearWing = 0, floorDmg = 0, diffuser = 0, sidepod = 0;
    bool drsFault = false, ersFault = false;
    double tWing = 0, tErs = 0, tDrs = 0, tFloor = 0;
    // track limits: the game's warning counters and the surface under the wheels
    uint8_t cutWarnings = 0, totalWarnings = 0;
    double offTrackUntil = -1;
    // the game's world frame is arbitrary: the outline is rotated, shifted and maybe mirrored into it
    float rotC = 1, rotS = 0, offX = 0, offZ = 0, mirror = 1;
    mutable std::mutex m;

    explicit Impl(uint64_t seed) : rng(uint32_t(seed ? seed : std::random_device{}()))
    {
        uid = seed ? seed : (uint64_t(rng()) << 32 | rng());
        profile(); // sets the track length
        std::normal_distribution<float> n(0, 1);
        std::uniform_real_distribution<double> ut(0, 1);
        const float ang = float(ut(rng) * 6.2831853);
        rotC = std::cos(ang);
        rotS = std::sin(ang);
        offX = float(ut(rng) * 2000 - 1000);
        offZ = float(ut(rng) * 2000 - 1000);
        mirror = ut(rng) < 0.5 ? 1.f : -1.f;
        tWing = 70 + 60 * ut(rng);   // front wing contact
        tErs = 150 + 60 * ut(rng);   // ERS fault for ~25 s
        tDrs = 260 + 60 * ut(rng);   // DRS fault for ~40 s
        tFloor = 200 + 80 * ut(rng); // floor damage from a kerb
        for (int i = 0; i < CARS; i++) {
            auto &c = cars[i];
            c.pace = 1.0f - 0.0016f * float(i) + 0.0012f * n(rng);
            if (i == PLAYER) c.pace = 0.9952f;
            c.visualTyre = i % 3 == 0 ? 18 : (i % 3 == 1 ? 17 : 16);
            c.tyreAge = uint8_t(i % 4);
            c.grid = uint8_t(i + 1);
            c.total = 260.0 - 13.0 * i;
            newLap(c, i);
            c.lapStart = c.sectorStart = -(c.total / 55.0); // as if they started at the line at ~200 km/h
        }
    }

    void newLap(Car &c, int i)
    {
        std::normal_distribution<float> n(0, 1);
        std::uniform_real_distribution<float> u(0, 1);
        c.lapPace = c.pace * (1.f + 0.0025f * n(rng)) * (1.f - 0.0004f * c.tyreAge);
        c.invalid = false;
        c.invalidAt = u(rng) < (i == PLAYER ? 0.12f : 0.03f) ? S1_END + 200 + u(rng) * (S2_END - S1_END - 400) : -1;
        c.s1 = c.s2 = 0;
        c.sector = 0;
    }

    void advance(double dt)
    {
        t += dt;
        const auto &P = profile();
        for (int i = 0; i < CARS; i++) {
            auto &c = cars[i];
            const float before = c.lapDist();
            const int lapBefore = c.lapNum();
            c.speed = Profile::get(P.v, before) * c.lapPace;
            c.total += c.speed * dt;
            const float after = c.lapDist();
            if (lapBefore != c.lapNum()) { // crossed the line
                Lap l;
                l.ms = uint32_t((t - c.lapStart) * 1000);
                l.s[0] = c.s1;
                l.s[1] = c.s2;
                l.s[2] = uint32_t((t - c.sectorStart) * 1000);
                l.valid = !c.invalid;
                if (c.s1 && c.s2) c.laps.push_back(l); // skip the partial first lap
                else c.laps.push_back({});
                c.lapStart = c.sectorStart = t;
                c.tyreAge++;
                c.wear += 2.2f + 0.3f * float(i % 3);
                newLap(c, i);
            } else {
                if (before < S1_END && after >= S1_END) {
                    c.s1 = uint32_t((t - c.sectorStart) * 1000);
                    c.sectorStart = t;
                    c.sector = 1;
                }
                if (before < S2_END && after >= S2_END) {
                    c.s2 = uint32_t((t - c.sectorStart) * 1000);
                    c.sectorStart = t;
                    c.sector = 2;
                }
                if (c.invalidAt > 0 && before < c.invalidAt && after >= c.invalidAt) {
                    c.invalid = true;
                    if (i == PLAYER) { // cut a corner: two wheels on the grass, a warning from the game
                        cutWarnings++;
                        totalWarnings++;
                        offTrackUntil = t + 0.7;
                    }
                }
            }
            const size_t b = size_t(std::max(0.0, c.total) / 10.0);
            while (firstAt.size() <= b) firstAt.push_back(float(t));
        }

        // player car temperatures and energy
        const auto &me = cars[PLAYER];
        const float d = me.lapDist(), brk = Profile::get(P.brk, d), thr = Profile::get(P.thr, d),
                    st = std::fabs(Profile::get(P.steer, d));
        for (int w = 0; w < 4; w++) {
            const float front = w >= 2 ? 1.f : 0.75f;
            const float bt = 380 + 650 * brk * front;
            brakeTemp[w] += (bt - brakeTemp[w]) * float(dt) * (bt > brakeTemp[w] ? 4.f : 0.6f);
            const float ts = 86 + 14 * brk * front + 10 * st + 4 * thr * (w < 2) + 0.15f * me.wear;
            tyreSurf[w] += (ts - tyreSurf[w]) * float(dt) * 0.8f;
        }
        ers = std::clamp(ers + float(dt) * (brk * 420000.f - (thr > 0.95f ? 160000.f : 0.f)), 0.f, 4.0e6f);

        // fuel burns with throttle (about 1.6 kg a lap)
        fuel = std::max(0.f, fuel - float(dt) * (0.0022f + 0.0255f * thr));
        // power unit wear creeps up, faster at full throttle
        static const float rates[6] = {0.0016f, 0.0011f, 0.0013f, 0.0008f, 0.0006f, 0.0014f};
        for (int k = 0; k < 6; k++) pu[k] = std::min(100.f, pu[k] + float(dt) * rates[k] * (0.4f + thr));
        gearbox = std::min(100.f, gearbox + float(dt) * 0.0005f);

        // scripted incidents so the status panel has something to show
        if (t >= tWing && t - dt < tWing) { wingL = 18; wingR = 6; }
        if (t >= tWing + 180 && t - dt < tWing + 180) { wingL = wingR = 0; } // as if the nose was changed
        if (t >= tFloor && t - dt < tFloor) { floorDmg = 9; diffuser = 4; }
        ersFault = t >= tErs && t < tErs + 25;
        drsFault = t >= tDrs && t < tDrs + 40;
    }

    PacketHeader header(PacketId id) const
    {
        PacketHeader h{};
        h.packetFormat = PACKET_FORMAT_2025;
        h.gameYear = 25;
        h.gameMajorVersion = 1;
        h.gameMinorVersion = 12;
        h.packetVersion = 1;
        h.packetId = uint8_t(id);
        h.sessionUID = uid;
        h.sessionTime = float(t);
        h.frameIdentifier = frame;
        h.overallFrameIdentifier = frame;
        h.playerCarIndex = PLAYER;
        h.secondaryPlayerCarIndex = 255;
        return h;
    }

    std::array<int, CARS> order() const
    {
        std::array<int, CARS> o;
        for (int i = 0; i < CARS; i++) o[i] = i;
        std::sort(o.begin(), o.end(), [&](int a, int b) { return cars[a].total > cars[b].total; });
        return o;
    }

    template <typename P> static void send(const P &p, const Out &out)
    {
        out(reinterpret_cast<const uint8_t *>(&p), sizeof p);
    }

    void sendParticipants(const Out &out)
    {
        PacketParticipants p{};
        p.header = header(PacketId::Participants);
        p.numActiveCars = CARS;
        for (int i = 0; i < CARS; i++) {
            auto &d = p.car[i];
            d.aiControlled = i != PLAYER;
            d.driverId = uint8_t(i);
            d.networkId = 255;
            d.teamId = TEAMS[i];
            d.raceNumber = NUMBERS[i];
            std::strncpy(d.name, NAMES[i], sizeof d.name - 1);
            d.yourTelemetry = 1;
            d.showOnlineNames = 1;
            d.platform = 255;
        }
        send(p, out);
    }

    void sendLapData(const Out &out)
    {
        PacketLapData p{};
        p.header = header(PacketId::LapData);
        const auto o = order();
        for (int pos = 0; pos < CARS; pos++) {
            const int i = o[pos];
            const auto &c = cars[i];
            auto &d = p.car[i];
            d.lastLapTimeInMS = c.laps.empty() ? 0 : c.laps.back().ms;
            d.currentLapTimeInMS = uint32_t(std::max(0.0, t - c.lapStart) * 1000);
            if (c.sector >= 1) putSplit(c.s1, d.sector1TimeMSPart, d.sector1TimeMinutesPart);
            if (c.sector >= 2) putSplit(c.s2, d.sector2TimeMSPart, d.sector2TimeMinutesPart);
            const size_t b = size_t(std::max(0.0, c.total) / 10.0);
            const float gapLeader = b < firstAt.size() ? float(t) - firstAt[b] : 0.f;
            float gapAhead = 0;
            if (pos > 0) {
                const size_t ba = size_t(std::max(0.0, cars[o[pos - 1]].total) / 10.0);
                const float ga = ba < firstAt.size() ? float(t) - firstAt[ba] : 0.f;
                gapAhead = std::max(0.f, gapLeader - ga);
            }
            putSplit(uint32_t(gapAhead * 1000), d.deltaToCarInFrontMSPart, d.deltaToCarInFrontMinutesPart);
            putSplit(pos ? uint32_t(gapLeader * 1000) : 0, d.deltaToRaceLeaderMSPart, d.deltaToRaceLeaderMinutesPart);
            d.lapDistance = c.lapDist();
            d.totalDistance = float(c.total);
            d.carPosition = uint8_t(pos + 1);
            d.currentLapNum = uint8_t(c.lapNum());
            d.sector = uint8_t(c.sector);
            d.currentLapInvalid = c.invalid;
            d.gridPosition = c.grid;
            d.driverStatus = 4;
            d.resultStatus = 2;
            d.speedTrapFastestLap = 255;
            if (i == PLAYER) {
                d.cornerCuttingWarnings = cutWarnings;
                d.totalWarnings = totalWarnings;
            }
        }
        p.timeTrialPBCarIdx = p.timeTrialRivalCarIdx = 255;
        send(p, out);
    }

    static void gearFor(float kmh, int8_t &gear, uint16_t &rpm, uint8_t &lights)
    {
        static const float top[9] = {0, 95, 135, 170, 205, 240, 270, 300, 400};
        int g = 1;
        while (g < 8 && kmh > top[g]) g++;
        const float lo = g == 1 ? 0 : top[g - 1] * 0.92f, hi = g == 8 ? 340.f : top[g];
        const float k = std::clamp((kmh - lo) / (hi - lo), 0.f, 1.f);
        gear = int8_t(g);
        rpm = uint16_t(std::clamp(7600.f + 4300.f * k, 4000.f, 11900.f));
        lights = uint8_t(std::clamp((float(rpm) - 9400.f) / (11800.f - 9400.f) * 100.f, 0.f, 100.f));
    }

    void sendTelemetry(const Out &out)
    {
        PacketCarTelemetry p{};
        p.header = header(PacketId::CarTelemetry);
        const auto &P = profile();
        for (int i = 0; i < CARS; i++) {
            const auto &c = cars[i];
            auto &d = p.car[i];
            const float dist = c.lapDist();
            d.speed = uint16_t(c.speed * 3.6f);
            d.throttle = Profile::get(P.thr, dist);
            d.brake = Profile::get(P.brk, dist);
            d.steer = Profile::get(P.steer, dist);
            gearFor(float(d.speed), d.gear, d.engineRPM, d.revLightsPercent);
            d.drs = (dist > L - 400 || dist < 200) && d.throttle > 0.99f;
            if (i == PLAYER) {
                const bool off = t < offTrackUntil;
                const bool kerb = std::fabs(Profile::get(P.curv, dist)) > 0.012f && std::fmod(dist, 37.f) < 9.f;
                const int inside = Profile::get(P.curv, dist) > 0 ? 0 : 1; // rl/fl or rr/fr
                for (int w = 0; w < 4; w++) {
                    Surface s = Surface::Tarmac;
                    if (off && w % 2 == 1) s = Surface::Grass;
                    else if (kerb && w % 2 == inside) s = Surface::RumbleStrip;
                    (&d.surfaceType.rl)[w] = uint8_t(s);
                }
            }
            for (int w = 0; w < 4; w++) {
                const float wobble = std::sin(float(t) * 0.7f + float(w)) * 1.5f;
                (&d.brakesTemperature.rl)[w] = uint16_t(i == PLAYER ? brakeTemp[w] : 500);
                (&d.tyresSurfaceTemperature.rl)[w] = uint8_t(i == PLAYER ? tyreSurf[w] + wobble : 90);
                (&d.tyresInnerTemperature.rl)[w] = uint8_t(i == PLAYER ? 96 + (tyreSurf[w] - 88) * 0.3f : 98);
                (&d.tyresPressure.rl)[w] = 22.6f + (w >= 2 ? 0.8f : 0.f) + (tyreSurf[w] - 88) * 0.03f;
            }
            d.engineTemperature = uint16_t(105 + 6 * d.throttle);
        }
        p.mfdPanelIndex = p.mfdPanelIndexSecondaryPlayer = 255;
        send(p, out);
    }

    void sendMotion(const Out &out)
    {
        PacketMotion p{};
        p.header = header(PacketId::Motion);
        const auto &P = profile();
        for (int i = 0; i < CARS; i++) {
            const auto &c = cars[i];
            auto &m = p.car[i];
            const float d = c.lapDist();
            const float h0 = P.heading[size_t(std::max(0.f, d)) % P.heading.size()];
            const float side = (float(i % 3) - 1.f) * 1.5f; // a little spread across the track
            const float ox = Profile::get(P.x, d) - std::sin(h0) * side, oz = Profile::get(P.z, d) + std::cos(h0) * side;
            // outline frame -> game world frame
            m.worldPositionX = rotC * ox - rotS * oz + offX;
            m.worldPositionY = 0;
            m.worldPositionZ = mirror * (rotS * ox + rotC * oz) + offZ;
            const float h = mirror > 0 ? h0 + std::atan2(rotS, rotC) : -(h0 + std::atan2(rotS, rotC));
            const float cx = std::cos(h), cz = std::sin(h);
            m.worldVelocityX = cx * c.speed;
            m.worldVelocityZ = cz * c.speed;
            m.worldForwardDirX = int16_t(cx * 32767);
            m.worldForwardDirZ = int16_t(cz * 32767);
            m.worldRightDirX = int16_t(cz * 32767);
            m.worldRightDirZ = int16_t(-cx * 32767);
            m.gForceLateral = c.speed * c.speed * Profile::get(P.curv, d) / 9.81f;
            m.gForceLongitudinal = (Profile::get(P.thr, d) * 1.2f - Profile::get(P.brk, d) * 4.5f);
            m.gForceVertical = 1.f;
            m.yaw = h;
        }
        send(p, out);
    }

    void sendMotionEx(const Out &out)
    {
        PacketMotionEx p{};
        p.header = header(PacketId::MotionEx);
        const auto &P = profile();
        const auto &c = cars[PLAYER];
        const float d = c.lapDist(), v = c.speed;
        const float thr = Profile::get(P.thr, d), brk = Profile::get(P.brk, d), st = Profile::get(P.steer, d);
        // loads: static weight + downforce, shifted forwards under braking and outwards in corners
        const float mass = 800.f, mu = 1.9f;
        const float down = 0.5f * 1.2f * 4.2f * v * v;
        const float base = (mass * 9.81f + down) / 4.f;
        const float fwd = brk * 0.28f - thr * 0.08f, side = st * 0.22f;
        float fz[4] = {base * (1 - fwd - side), base * (1 - fwd + side), base * (1 + fwd - side), base * (1 + fwd + side)};
        // grip use: braking near the limit, corners near the limit, traction on the rears at low speed
        std::uniform_real_distribution<float> jitter(-0.03f, 0.03f);
        const float uLat = std::min(1.f, std::fabs(st) * 1.15f), uBrk = brk * 0.96f;
        const float uThr = thr * std::clamp(1.2f - v / 60.f, 0.15f, 0.95f);
        for (int w = 0; w < 4; w++) {
            const bool rear = w < 2;
            float ux = rear ? uThr - uBrk * 0.9f : -uBrk;
            float uy = uLat * (st >= 0 ? 1.f : -1.f);
            float u = std::sqrt(ux * ux + uy * uy);
            const float cap = 0.98f + jitter(rng) + (u > 0.9f && !rear && brk > 0.8f ? 0.06f : 0.f); // fronts lock now and then
            if (u > cap) { ux *= cap / u; uy *= cap / u; }
            (&p.wheelLongForce.rl)[w] = ux * mu * fz[w];
            (&p.wheelLatForce.rl)[w] = uy * mu * fz[w];
            (&p.wheelVertForce.rl)[w] = fz[w];
            (&p.wheelSlipRatio.rl)[w] = ux * 0.09f;
            (&p.wheelSlipAngle.rl)[w] = uy * 0.12f;
            (&p.wheelSpeed.rl)[w] = v * (1 + ux * 0.09f);
        }
        p.localVelocityZ = v;
        p.frontWheelsAngle = st * 0.3f;
        send(p, out);
    }

    void sendSession(const Out &out)
    {
        PacketSession p{};
        p.header = header(PacketId::Session);
        p.weather = 0;
        p.trackTemperature = 38;
        p.airTemperature = 26;
        p.totalLaps = 30;
        p.trackLength = uint16_t(L);
        p.sessionType = 15; // race
        p.trackId = SIM_TRACK;
        p.sessionTimeLeft = 7200;
        p.sessionDuration = 7200;
        p.pitSpeedLimit = 80;
        p.safetyCarStatus = 0;
        p.cornerCuttingStringency = 1;
        p.sector2LapDistanceStart = S1_END;
        p.sector3LapDistanceStart = S2_END;
        send(p, out);
    }

    void sendStatus(const Out &out)
    {
        PacketCarStatus p{};
        p.header = header(PacketId::CarStatus);
        for (int i = 0; i < CARS; i++) {
            const auto &c = cars[i];
            auto &d = p.car[i];
            d.tractionControl = 0;
            d.fuelMix = 1;
            d.frontBrakeBias = 56;
            d.fuelInTank = i == PLAYER ? fuel : 40.f;
            d.fuelCapacity = 110;
            d.fuelRemainingLaps = i == PLAYER ? fuel / 1.6f - (30.f - float(c.lapNum())) : 1.f;
            d.maxRPM = 12000;
            d.idleRPM = 4000;
            d.maxGears = 8;
            d.drsAllowed = c.lapDist() > L - 500 || c.lapDist() < 200;
            d.actualTyreCompound = uint8_t(c.visualTyre == 16 ? 19 : c.visualTyre == 17 ? 18 : 17);
            d.visualTyreCompound = c.visualTyre;
            d.tyresAgeLaps = c.tyreAge;
            d.vehicleFiaFlags = 0;
            d.enginePowerICE = 560000;
            d.enginePowerMGUK = 120000;
            d.ersStoreEnergy = i == PLAYER ? ers : 2.0e6f;
            d.ersDeployMode = 1;
        }
        send(p, out);
    }

    void sendDamage(const Out &out)
    {
        PacketCarDamage p{};
        p.header = header(PacketId::CarDamage);
        for (int i = 0; i < CARS; i++) {
            const float w = cars[i].wear;
            p.car[i].tyresWear = {w * 1.05f, w * 1.1f, w * 0.9f, w * 0.95f};
        }
        auto &d = p.car[PLAYER];
        auto u8 = [](float v) { return uint8_t(std::clamp(v, 0.f, 100.f)); };
        d.frontLeftWingDamage = u8(wingL);
        d.frontRightWingDamage = u8(wingR);
        d.rearWingDamage = u8(rearWing);
        d.floorDamage = u8(floorDmg);
        d.diffuserDamage = u8(diffuser);
        d.sidepodDamage = u8(sidepod);
        d.drsFault = drsFault;
        d.ersFault = ersFault;
        d.gearBoxDamage = u8(gearbox);
        d.engineDamage = u8(pu[0] * 0.5f);
        d.engineICEWear = u8(pu[0]);
        d.engineMGUHWear = u8(pu[1]);
        d.engineMGUKWear = u8(pu[2]);
        d.engineESWear = u8(pu[3]);
        d.engineCEWear = u8(pu[4]);
        d.engineTCWear = u8(pu[5]);
        const float tw = cars[PLAYER].wear;
        d.tyresDamage = {u8(tw * 0.3f), u8(tw * 0.3f), u8(tw * 0.25f), u8(tw * 0.25f)};
        send(p, out);
    }

    void sendHistory(int i, const Out &out)
    {
        PacketSessionHistory p{};
        p.header = header(PacketId::SessionHistory);
        const auto &c = cars[i];
        p.carIdx = uint8_t(i);
        const int n = std::min<int>(int(c.laps.size()), MAX_LAPS_IN_HISTORY - 1);
        const int first = int(c.laps.size()) - n;
        p.numLaps = uint8_t(n + 1); // the running lap is listed too, with zeros
        p.numTyreStints = 1;
        uint32_t best = 0, bestS[3] = {};
        for (int k = 0; k < n; k++) {
            const Lap &l = c.laps[first + k];
            auto &d = p.lap[k];
            d.lapTimeInMS = l.ms;
            putSplit(l.s[0], d.sector1TimeMSPart, d.sector1TimeMinutesPart);
            putSplit(l.s[1], d.sector2TimeMSPart, d.sector2TimeMinutesPart);
            putSplit(l.s[2], d.sector3TimeMSPart, d.sector3TimeMinutesPart);
            d.lapValidBitFlags = l.ms ? uint8_t(l.valid ? 0xF : 0x0B) : 0; // invalid laps lose the S2 bit
            if (!l.ms) continue;
            if (l.valid && (!best || l.ms < best)) { best = l.ms; p.bestLapTimeLapNum = uint8_t(k + 1); }
            uint8_t *nums[3] = {&p.bestSector1LapNum, &p.bestSector2LapNum, &p.bestSector3LapNum};
            for (int s = 0; s < 3; s++)
                if ((s != 1 || l.valid) && (!bestS[s] || l.s[s] < bestS[s])) {
                    bestS[s] = l.s[s];
                    *nums[s] = uint8_t(k + 1);
                }
        }
        p.stint[0] = {255, uint8_t(c.visualTyre == 16 ? 19 : c.visualTyre == 17 ? 18 : 17), c.visualTyre};
        send(p, out);
    }

    void tick(const Out &out)
    {
        advance(TICK);
        frame++;
        if (frame == 1 || frame % 300 == 0) sendParticipants(out);
        if (frame == 1 || frame % 120 == 0) sendSession(out);
        sendTelemetry(out);
        if (frame % 2 == 1) sendMotion(out);
        sendMotionEx(out);
        if (frame % 2 == 0) sendLapData(out);
        if (frame % 6 == 0) sendStatus(out);
        if (frame % 30 == 0) sendDamage(out);
        if (frame % 3 == 0) {
            sendHistory(historyCar, out);
            historyCar = (historyCar + 1) % CARS;
        }
    }
};

Sim::Sim(uint64_t seed) : d_(std::make_unique<Impl>(seed)) {}

Sim::~Sim() { stop(); }

void Sim::step(double dt, const Out &out)
{
    std::lock_guard<std::mutex> l(d_->m);
    d_->acc += dt;
    while (d_->acc >= TICK) {
        d_->acc -= TICK;
        d_->tick(out);
    }
}

void Sim::start(Out out, double speed)
{
    stop();
    running_ = true;
    thread_ = std::thread([this, out = std::move(out), speed] {
        auto last = std::chrono::steady_clock::now();
        while (running_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            const auto now = std::chrono::steady_clock::now();
            step(std::chrono::duration<double>(now - last).count() * speed, out);
            last = now;
        }
    });
}

void Sim::stop()
{
    running_ = false;
    if (thread_.joinable()) thread_.join();
}

double Sim::time() const
{
    std::lock_guard<std::mutex> l(d_->m);
    return d_->t;
}

float Sim::trackLength() { return L; }

} // namespace f1sim
