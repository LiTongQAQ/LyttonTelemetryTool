// The simulator's packets must parse, drive RaceState / TraceLog like the game would, and survive record + replay.
#include "hub.hpp"
#include "race_state.hpp"
#include "replay.hpp"
#include "sim.hpp"
#include "trace.hpp"
#include "tracks.hpp"

#include <f1tel/parse.hpp>

#include <cstdio>
#include <filesystem>
#include <set>

static int failures = 0;
#define CHECK(cond)                                                                                                    \
    do {                                                                                                               \
        if (!(cond)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); failures++; }                       \
    } while (0)

int main()
{
    f1sim::Sim sim(42);
    timing::RaceState race;
    timing::TraceLog trace;
    f1hub::Recorder rec;
    const std::string path = (std::filesystem::temp_directory_path() / "f1sim_test.f1tr").string();
    CHECK(rec.open(path));
    uint64_t bad = 0, n = 0;
    double now = 0;
    auto out = [&](const uint8_t *d, size_t len) {
        n++;
        if (!f1tel::read_header(d, len)) bad++;
        race.feed(d, len);
        trace.feed(d, len);
        rec.write(uint64_t(now * 1e9), d, len);
    };
    for (int i = 0; i < 200 * 20; i++) { now += 0.05; sim.step(0.05, out); } // 200 s
    rec.close();
    CHECK(bad == 0);
    CHECK(n > 10000);

    auto s = race.snapshot();
    CHECK(s.numActive == 20);
    CHECK(s.playerIdx == 4);
    std::set<int> pos;
    for (int i : s.order()) pos.insert(s.cars[i].position);
    CHECK(pos.size() == 20);
    CHECK(s.cars[4].laps.size() >= 1);
    CHECK(s.sessionBestLapMs > 60000 && s.sessionBestLapMs < 120000);
    CHECK(s.player.speed > 0);
    CHECK(s.wheels.valid && s.wheels.load[2] > 1000);
    CHECK(s.session.valid && s.session.trackId == 11);
    CHECK(s.damage.valid && s.damage.ice > 0);
    CHECK(s.fuel.avgLapKg > 1.0f && s.fuel.avgLapKg < 2.5f);
    CHECK(s.cars[4].hasPosition);
    std::printf("fuel %.2f kg/lap, FL load %.0f N, events %zu, cut warnings %u\n", s.fuel.avgLapKg, s.wheels.load[2],
                s.events.size(), unsigned(s.player.cornerCuttingWarnings));
    std::printf("best lap %s, player lap %d\n", timing::formatLap(s.sessionBestLapMs).c_str(), s.cars[4].lapNum);

    int complete = 0;
    trace.read([&](const timing::TraceData &d) {
        for (const auto &l : d.laps) complete += l.complete;
        CHECK(d.trackLength > 5000);
        CHECK(!d.recent.empty());
        // the bundled outline lines up with the simulated (rotated, shifted) world positions
        std::vector<tracks::DrivenPoint> pts;
        for (const auto &l : d.laps)
            for (const auto &q : l.s) pts.push_back({q.dist, q.x, q.z});
        auto g = tracks::align(*tracks::forGame(11), pts, float(s.session.trackLength));
        std::printf("track fit: ok %d, rms %.1f m, scale %.3f\n", g.ok, g.rms, g.scale);
        CHECK(g.ok && g.rms < 5.f);
    });
    CHECK(complete >= 1);

    // replay: seek forward and back gives the same state as the live feed
    timing::RaceState r2;
    f1hub::Replayer rp([&](const uint8_t *d, size_t len) { r2.feed(d, len); }, [&] { r2.reset(); });
    CHECK(rp.open(path));
    CHECK(rp.packets() == n);
    CHECK(!rp.laps().empty());
    rp.seek(rp.duration());
    CHECK(r2.snapshot().cars[4].laps.size() == s.cars[4].laps.size());
    rp.seek(rp.duration() / 4);
    CHECK(r2.snapshot().cars[4].laps.size() < s.cars[4].laps.size());
    rp.seek(rp.duration());
    CHECK(r2.snapshot().sessionBestLapMs == s.sessionBestLapMs);
    auto info = f1hub::scanRecording(path);
    CHECK(info.ok && info.packets == n);
    std::filesystem::remove(path);

    std::printf(failures ? "%d failure(s)\n" : "sim OK\n", failures);
    return failures ? 1 : 0;
}
