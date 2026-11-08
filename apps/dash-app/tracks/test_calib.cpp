// Track calibration on synthetic data: the bundled Monza outline placed in a known "game" frame, an averaged line
// that wanders like a racing line, and kerb points on the edges with noise. The calibrated map must land on the truth.
#include "calib.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <random>

static int failures = 0;
#define CHECK(cond)                                                                                                    \
    do {                                                                                                               \
        if (!(cond)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); failures++; }                       \
    } while (0)

// JSON for looking at a calibration in a browser: both maps, the consensus edge points, a sample of positions.
static void dumpJson(const std::string &path, const tracks::CalData &d)
{
    using namespace tracks;
    CalReport ro, rd;
    const Geometry o = calibrate(d, &ro, CalMethod::Outline), g = calibrate(d, &rd, CalMethod::Data);
    std::FILE *f = std::fopen(path.c_str(), "wb");
    if (!f) return;
    auto geo = [&](const char *name, const Geometry &m) {
        std::fprintf(f, "\"%s\":{\"ok\":%s,\"l\":[", name, m.ok ? "true" : "false");
        for (size_t i = 0; i < m.lx.size(); i++) std::fprintf(f, "%s[%.1f,%.1f]", i ? "," : "", m.lx[i], m.lz[i]);
        std::fprintf(f, "],\"r\":[");
        for (size_t i = 0; i < m.rx.size(); i++) std::fprintf(f, "%s[%.1f,%.1f]", i ? "," : "", m.rx[i], m.rz[i]);
        std::fprintf(f, "]},");
    };
    std::fprintf(f, "{");
    geo("outline", o);
    geo("data", g);
    float spread = 0;
    std::fprintf(f, "\"edges\":[");
    bool first = true;
    for (const auto &e : d.edges) {
        (void)spread;
        std::fprintf(f, "%s[%.1f,%.1f,\"%c\"]", first ? "" : ",", e.x, e.z, e.kind);
        first = false;
    }
    std::fprintf(f, "],\"cell\":%.2f,\"surf\":[", o.cell);
    first = true;
    for (const auto &[key, v] : d.grid) { // every scanned cell, tarmac too, for looking at
        int best = 0;
        for (int k = 1; k < S_CLASSES; k++)
            if (v[k] > v[best]) best = k;
        std::fprintf(f, "%s[%d,%d,%d]", first ? "" : ",", int32_t(uint32_t(key >> 32)), int32_t(uint32_t(key)), best);
        first = false;
    }
    std::fprintf(f, "],\"pos\":[");
    first = true;
    for (size_t i = 0; i < d.pos.size(); i++)
        for (size_t k = 0; k < d.pos[i].size(); k += 6) {
            std::fprintf(f, "%s[%.1f,%.1f]", first ? "" : ",", d.pos[i][k][0], d.pos[i][k][1]);
            first = false;
        }
    std::fprintf(f, "]}");
    std::fclose(f);
}

// test_trackcal --display full.f1map out.f1map: the 2D map (no scan data) of a full map.
static int makeDisplay(const char *in, const char *out)
{
    tracks::Geometry g;
    int id = -1;
    if (!tracks::loadMap(in, g, &id) || id < 0) { std::printf("%s: not a map file\n", in); return 1; }
    const bool ok = tracks::saveDisplayMap(g, id, out);
    std::printf("2D map %s: %s (%zu points, %zu regions)\n", out, ok ? "written" : "FAILED", g.cx.size(), g.bands.size());
    return ok ? 0 : 1;
}

// test_trackcal rec1.f1tr [rec2.f1tr ...] [--json out.json]: extract and calibrate real recordings, print the reports.
static int runOnRecordings(int argc, char **argv)
{
    using namespace tracks;
    std::vector<std::string> files;
    std::string json, mapOut, dataOut; // --map: the calibrated map (with its data); --data: the data file
    for (int i = 1; i < argc; i++) {
        if (std::string(argv[i]) == "--json" && i + 1 < argc) json = argv[++i];
        else if (std::string(argv[i]) == "--map" && i + 1 < argc) mapOut = argv[++i];
        else if (std::string(argv[i]) == "--data" && i + 1 < argc) dataOut = argv[++i];
        else files.push_back(argv[i]);
    }
    std::vector<std::string> log;
    auto sets = extract(files, nullptr, &log);
    for (const auto &l : log) std::printf("%s\n", l.c_str());
    for (const auto &d : sets) {
        if (!json.empty()) dumpJson(json, d);
        if (!dataOut.empty()) std::printf("data %s: %s\n", dataOut.c_str(), saveData(d, dataOut) ? "written" : "FAILED");
        std::printf("game year %d\n", d.gameYear);
        size_t byClass[S_CLASSES] = {};
        for (const auto &[key, v] : d.grid) {
            int best = 0;
            for (int k = 1; k < S_CLASSES; k++)
                if (v[k] > v[best]) best = k;
            byClass[best]++;
        }
        std::printf("surface scan: %zu cells (tarmac %zu, kerb %zu, grass %zu, gravel/sand %zu, hard %zu, water %zu)\n",
                    d.grid.size(), byClass[0], byClass[1], byClass[2], byClass[3], byClass[4], byClass[5]);
        CalReport r;
        Geometry g = calibrate(d, &r);
        if (!mapOut.empty() && g.ok) std::printf("map %s: %s\n", mapOut.c_str(), saveMap(g, d.trackId, r, d.sources, mapOut, nullptr, &d) ? "written" : "FAILED");
        std::printf("track %d (%s): %llu samples, %zu kerbs, coverage %.0f%% -> %s\n", d.trackId, asciiName(d.trackId).c_str(),
                    (unsigned long long)d.samples, d.edges.size(), d.coverage() * 100.f, r.message.c_str());
        if (const Shape *sh = forGame(d.trackId)) { // what the first alignment sees, without its limits
            std::vector<DrivenPoint> pts;
            for (size_t i = 0; i < d.bins.size(); i++)
                if (d.bins[i].n >= 2)
                    pts.push_back({(float(i) + 0.5f) * CAL_BIN_M, float(d.bins[i].sx / d.bins[i].n), float(d.bins[i].sz / d.bins[i].n)});
            Geometry a = align(*sh, pts, d.trackLength, 1e9f);
            // the averaged line itself: neighbouring 5 m bins should be about 5 m apart
            int jumps = 0;
            double maxStep = 0;
            for (size_t k = 1; k < pts.size(); k++) {
                const double st = std::hypot(pts[k].x - pts[k - 1].x, pts[k].z - pts[k - 1].z);
                maxStep = std::max(maxStep, st);
                if (st > 15) {
                    if (jumps < 8) std::printf("  jump at %.0f m: %.1f m (n %u)\n", pts[k].dist, st, d.bins[size_t(pts[k].dist / CAL_BIN_M)].n);
                    jumps++;
                }
            }
            std::printf("  averaged line: %zu bins, %d jumps over 15 m, largest step %.1f m\n", pts.size(), jumps, maxStep);
            std::printf("  outline %s (%s): %.0f m, game %.0f m; first alignment rms %.1f m, scale %.3f\n", sh->name.c_str(),
                        sh->source.c_str(), sh->length, d.trackLength, a.rms, a.scale);
        }
    }
    return 0;
}

int main(int argc, char **argv)
{
    using namespace tracks;
    if (argc == 4 && std::string(argv[1]) == "--display") return makeDisplay(argv[2], argv[3]);
    if (argc > 1) return runOnRecordings(argc, argv);
    const Shape *sh = forGame(11); // Monza
    CHECK(sh != nullptr);
    if (!sh) return 1;

    // the "game" frame: rotated 37 deg, moved, mirrored like the game's z axis
    const float ang = 37.f * 3.14159265f / 180.f, ca = std::cos(ang), sa = std::sin(ang);
    auto toGame = [&](float x, float y, float &gx, float &gz) {
        gx = ca * x - sa * y + 120.f;
        gz = -(sa * x + ca * y) - 340.f;
    };
    // outline points with the left / right edges, every point of the shape
    const int n = int(sh->x.size());
    std::vector<float> cum(n, 0);
    for (int i = 1; i < n; i++) cum[i] = cum[i - 1] + std::hypot(sh->x[i] - sh->x[i - 1], sh->y[i] - sh->y[i - 1]);
    const float L = cum[n - 1] + std::hypot(sh->x[0] - sh->x[n - 1], sh->y[0] - sh->y[n - 1]);

    std::mt19937 rng(7);
    std::normal_distribution<float> noise(0.f, 0.25f);
    CalData d;
    d.trackId = 11;
    d.trackLength = L;
    d.bins.assign(size_t(std::ceil(L / CAL_BIN_M)), {});
    std::vector<float> truthX, truthZ, truthD;
    for (int i = 0; i < n; i++) {
        const int j = (i + 1) % n, k = (i + n - 1) % n;
        float tx = sh->x[j] - sh->x[k], ty = sh->y[j] - sh->y[k];
        const float tl = std::hypot(tx, ty);
        tx /= tl; ty /= tl;
        const float nx = -ty, ny = tx; // left
        float gx, gz;
        toGame(sh->x[i], sh->y[i], gx, gz);
        truthX.push_back(gx); truthZ.push_back(gz); truthD.push_back(cum[i]);
        // averaged line: wanders +-2.5 m around the centre
        for (int lane = -1; lane <= 1; lane++) { // three cars on slightly different lines
            const float wob = 2.5f * std::sin(cum[i] / 90.f) + 0.8f * float(lane);
            toGame(sh->x[i] + nx * wob, sh->y[i] + ny * wob, gx, gz);
            auto &b = d.bins[std::min(d.bins.size() - 1, size_t(cum[i] / CAL_BIN_M))];
            b.sx += gx; b.sz += gz; b.n++;
            d.samples++;
        }
        // kerbs on 40 % of the lap, alternating sides
        if (std::fmod(cum[i], 250.f) < 100.f) {
            const bool left = std::fmod(cum[i], 500.f) < 250.f;
            const float w = left ? sh->wl[i] : -sh->wr[i];
            toGame(sh->x[i] + nx * w, sh->y[i] + ny * w, gx, gz);
            for (int lap = 0; lap < 4; lap++) // four laps over the same kerb
                d.edges.push_back({cum[i], gx + noise(rng), gz + noise(rng), left ? 'L' : 'R', 'I'});
        }
    }

    CalReport r;
    Geometry g = calibrate(d, &r);
    std::printf("%s\n", r.message.c_str());
    CHECK(g.ok && r.ok);
    if (!g.ok) return 1;
    // every map centre point vs the truth centre line (distance to the polyline, not to its vertices)
    auto toTruth = [&](double x, double z) {
        double best = 1e18;
        const size_t t = truthX.size();
        for (size_t k = 0; k < t; k++) {
            const size_t k2 = (k + 1) % t;
            const double ax = truthX[k], az = truthZ[k], bx = truthX[k2] - ax, bz = truthZ[k2] - az;
            const double l2 = bx * bx + bz * bz;
            const double u = l2 > 0 ? std::clamp(((x - ax) * bx + (z - az) * bz) / l2, 0.0, 1.0) : 0.0;
            best = std::min(best, std::hypot(x - ax - u * bx, z - az - u * bz));
        }
        return best;
    };
    auto rmsOf = [&](const Geometry &geo) {
        double s = 0;
        for (size_t i = 0; i < geo.cx.size(); i++) {
            const double e = toTruth(geo.cx[i], geo.cz[i]);
            s += e * e;
        }
        return geo.cx.empty() ? 1e9 : std::sqrt(s / double(geo.cx.size()));
    };
    std::vector<DrivenPoint> pts;
    for (size_t i = 0; i < d.bins.size(); i++)
        if (d.bins[i].n >= 2)
            pts.push_back({(float(i) + 0.5f) * CAL_BIN_M, float(d.bins[i].sx / d.bins[i].n), float(d.bins[i].sz / d.bins[i].n)});
    const double before = rmsOf(align(*sh, pts, d.trackLength, 40.f)), after = rmsOf(g);
    std::printf("centre vs truth: %.2f m after the first alignment, %.2f m calibrated\n", before, after);
    CHECK(after < 0.5);
    CHECK(after < before);
    CHECK(r.kerbLocal <= r.kerbBefore + 0.01f);

    // Track N Race lines (third_party/track-n-race): Melbourne in game coordinates, with its DRS zones
    {
        const TnrTrack *t = tnrForGame(0);
        CHECK(t && t->x.size() > 1000 && t->length == 5276.f && !t->zones.empty());
        if (t) {
            const Geometry g = fromTnr(*t, 5276.f);
            CHECK(g.ok && g.cx.size() > 900 && std::fabs(g.dist.back() - 5276.f) < 10.f && !g.zones.empty());
            std::printf("Track N Race Melbourne: %zu points, %zu zones\n", g.cx.size(), g.zones.size());
        }
        CHECK(tnrForGame(1) == nullptr); // Paul Ricard: not in F1 25
    }
    // integrating: a full map carries its scan data; two of them merge and calibrate again
    {
        CalReport r0;
        const Geometry g0 = calibrate(d, &r0);
        const std::string mp = (std::filesystem::temp_directory_path() / "f1race_full.f1map").string();
        CHECK(g0.ok && saveMap(g0, d.trackId, r0, {"a.f1tr"}, mp, nullptr, &d));
        Geometry g1;
        CalData d1;
        bool has = false;
        CHECK(loadMap(mp, g1, nullptr, nullptr, &d1, &has) && has && d1.samples == d.samples);
        CalData both = d1;
        CHECK(mergeData(both, d1) && both.samples == 2 * d1.samples);
        CalReport ri;
        CHECK(calibrate(both, &ri).ok);
        const std::string dp = (std::filesystem::temp_directory_path() / "f1race_display.f1map").string();
        CHECK(saveDisplayMap(g0, d.trackId, dp));
        Geometry g2;
        CalData d2;
        bool has2 = true;
        CHECK(loadMap(dp, g2, nullptr, nullptr, &d2, &has2) && !has2 && g2.cx.size() == g0.cx.size());
        std::filesystem::remove(mp);
        std::filesystem::remove(dp);
    }

    // data files: save, load, merge
    const auto dir = std::filesystem::temp_directory_path();
    const std::string a = (dir / "f1race_cal_a.f1cal").string();
    d.sources = {"a.f1tr"};
    CHECK(saveData(d, a));
    CalData back;
    CHECK(loadData(a, back));
    CHECK(back.trackId == 11 && back.edges.size() == d.edges.size() && back.samples == d.samples);
    CalData both = back;
    CHECK(mergeData(both, back));
    CHECK(both.samples == 2 * d.samples && both.edges.size() == 2 * d.edges.size() && both.sources.size() == 2);
    CalData other;
    other.trackId = 12;
    CHECK(!mergeData(both, other));

    // map files
    const std::string mp = (dir / "f1race_cal.f1map").string();
    CHECK(saveMap(g, 11, r, d.sources, mp));
    Geometry g2;
    int id = -1;
    CHECK(loadMap(mp, g2, &id) && id == 11 && g2.cx.size() == g.cx.size());
    CHECK(std::fabs(g2.lx[10] - g.lx[10]) < 0.01f);
    std::filesystem::remove(a);
    std::filesystem::remove(mp);

    std::printf(failures ? "%d FAILED\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
