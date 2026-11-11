// RaceState on real captures plus a synthetic session history for the sector colours.
#include "race_state.hpp"

#include <f1tel/parse.hpp>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

static int failures = 0;
#define CHECK(cond)                                                                                                    \
    do {                                                                                                               \
        if (!(cond)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); failures++; }                       \
    } while (0)

static std::vector<uint8_t> load(const char *name)
{
    std::ifstream f(std::string(F1TEL_TEST_DATA) + "/" + name, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), {}};
}

static void feed(timing::RaceState &rs, const std::vector<uint8_t> &b) { rs.feed(b.data(), b.size()); }

// A session history packet for one car, built on a real header.
static std::vector<uint8_t> history(const std::vector<uint8_t> &headerFrom, uint8_t car,
                                    std::vector<std::array<uint32_t, 3>> laps, uint8_t invalidSectorMask = 0)
{
    f1tel::PacketSessionHistory p{};
    std::memcpy(&p.header, headerFrom.data(), sizeof p.header);
    p.header.packetId = uint8_t(f1tel::PacketId::SessionHistory);
    p.carIdx = car;
    p.numLaps = uint8_t(laps.size());
    uint32_t best = 0;
    uint8_t bestNum[3] = {}, bestLap = 0;
    uint32_t bestS[3] = {};
    for (size_t k = 0; k < laps.size(); k++) {
        auto &d = p.lap[k];
        auto s = laps[k];
        d.sector1TimeMSPart = uint16_t(s[0] % 60000); d.sector1TimeMinutesPart = uint8_t(s[0] / 60000);
        d.sector2TimeMSPart = uint16_t(s[1] % 60000); d.sector2TimeMinutesPart = uint8_t(s[1] / 60000);
        d.sector3TimeMSPart = uint16_t(s[2] % 60000); d.sector3TimeMinutesPart = uint8_t(s[2] / 60000);
        d.lapTimeInMS = s[0] + s[1] + s[2];
        d.lapValidBitFlags = uint8_t(0xF & ~invalidSectorMask);
        if (!best || d.lapTimeInMS < best) { best = d.lapTimeInMS; bestLap = uint8_t(k + 1); }
        for (int j = 0; j < 3; j++)
            if (!bestS[j] || s[j] < bestS[j]) { bestS[j] = s[j]; bestNum[j] = uint8_t(k + 1); }
    }
    p.bestLapTimeLapNum = bestLap;
    p.bestSector1LapNum = bestNum[0]; p.bestSector2LapNum = bestNum[1]; p.bestSector3LapNum = bestNum[2];
    std::vector<uint8_t> out(sizeof p);
    std::memcpy(out.data(), &p, sizeof p);
    return out;
}

int main()
{
    using namespace timing;
    auto parts = load("Participants.bin"), lap = load("LapData.bin"), sta = load("CarStatus.bin"),
         tel = load("CarTelemetry.bin"), hist = load("SessionHistory.bin");

    RaceState rs;
    feed(rs, parts);
    feed(rs, lap);
    feed(rs, sta);
    feed(rs, tel);
    feed(rs, hist);
    auto s = rs.snapshot();

    CHECK(s.hasData);
    CHECK(s.numActive == 20);
    CHECK(s.playerIdx == 19);
    CHECK(s.cars[19].name == "RUSSELL");
    CHECK(s.cars[17].name == "HAMILTON");
    CHECK(s.cars[20].active == false);
    CHECK(shortName(s.cars[17].name) == "HAM");
    CHECK(shortName("Max Verstappen") == "VER");

    auto order = s.order();
    CHECK(order.size() == 20);
    for (size_t k = 1; k < order.size(); k++) CHECK(s.cars[order[k - 1]].position < s.cars[order[k]].position);
    std::printf("P1 %s, player P%u, %zu cars\n", s.cars[order[0]].name.c_str(), s.cars[19].position, order.size());
    CHECK(s.player.throttle >= 0 && s.player.throttle <= 1);
    CHECK(s.player.rpm == 4819); // the capture's player car (see test_f1tel)
    CHECK(s.cars[19].laps.empty()); // capture is on lap 1: nothing completed yet

    // Sector colours: car 0 sets the session best S1, the player beats only their own S2.
    feed(rs, history(hist, 0, {{30000, 40000, 25000}}));
    feed(rs, history(hist, 19, {{30500, 41000, 26000}, {30800, 40500, 25500}}));
    s = rs.snapshot();
    CHECK(s.sessionBestSectorMs[0] == 30000);
    CHECK(s.sessionBestSectorMs[1] == 40000);
    CHECK(s.cars[19].laps.size() == 2);
    CHECK(s.cars[19].bestSectorMs[1] == 40500);
    CHECK(markSector(s, 0, 0, 30000, true) == Mark::SessionBest);
    CHECK(markSector(s, 19, 1, 40500, true) == Mark::PersonalBest);
    CHECK(markSector(s, 19, 0, 30800, true) == Mark::Slower);
    CHECK(markSector(s, 19, 0, 30800, false) == Mark::Invalid);
    CHECK(markLap(s, 0, 95000, true) == Mark::SessionBest);

    // Sector cells: just after the line the finished lap's three are held, later all three reset together.
    {
        timing::Snapshot t = s;
        auto &c = t.cars[19];
        c.sector = 0;
        c.currentLapMs = 1500;
        auto cells = lapSectors(t, 19);
        CHECK(cells[0].state == SectorCell::Held && cells[2].state == SectorCell::Held);
        CHECK(cells[1].ms == 40500 && cells[1].mark == Mark::PersonalBest);
        c.currentLapMs = SECTOR_HOLD_MS + 1;
        cells = lapSectors(t, 19);
        CHECK(cells[0].state == SectorCell::Running && cells[1].state == SectorCell::Todo && cells[2].state == SectorCell::Todo);
        CHECK(cells[1].ms == 0 && cells[2].ms == 0);
        c.sector = 1;
        c.currentLapMs = 31000;
        c.currentSectorMs[0] = 30200;
        cells = lapSectors(t, 19);
        CHECK(cells[0].state == SectorCell::Done && cells[0].ms == 30200 && cells[0].mark == Mark::PersonalBest);
        CHECK(cells[1].state == SectorCell::Running && cells[2].state == SectorCell::Todo);
        // in / out laps and the pit lane, from the game's driver and pit status
        CHECK(std::string(runningLabel(c)) == "LIVE");
        c.driverStatus = 2;
        CHECK(std::string(runningLabel(c)) == "IN LAP");
        c.pitStatus = 1;
        CHECK(std::string(runningLabel(c)) == "PIT");
        c.pitStatus = 0;
        c.driverStatus = 3;
        CHECK(std::string(runningLabel(c)) == "OUT LAP");
    }

    // Invalid sectors don't set the session best.
    feed(rs, history(hist, 5, {{29000, 39000, 24000}}, 0x2 | 0x1));
    s = rs.snapshot();
    CHECK(s.sessionBestSectorMs[0] == 30000);
    CHECK(s.sessionBestSectorMs[1] == 39000);

    // Compact tower: leader, two ahead, you, two behind; leading / last / near the ends stay at 6 rows.
    {
        Snapshot r;
        for (int i = 0; i < 20; i++) {
            r.cars[i].active = true;
            r.cars[i].position = uint8_t(i + 1); // car index i is P(i+1)
        }
        auto positions = [&](int me) {
            r.playerIdx = uint8_t(me);
            std::vector<int> p;
            for (int i : r.compactOrder()) p.push_back(r.cars[i].position);
            return p;
        };
        CHECK((positions(9) == std::vector<int>{1, 8, 9, 10, 11, 12}));   // P10: leader + P8 P9 [P10] P11 P12
        CHECK((positions(0) == std::vector<int>{1, 2, 3, 4, 5, 6}));      // leading: you + next five
        CHECK((positions(19) == std::vector<int>{15, 16, 17, 18, 19, 20})); // last: the five ahead + you
        CHECK((positions(1) == std::vector<int>{1, 2, 3, 4, 5, 6}));      // P2: top six
        CHECK((positions(2) == std::vector<int>{1, 2, 3, 4, 5, 6}));      // P3: window reaches the leader
        CHECK((positions(3) == std::vector<int>{1, 2, 3, 4, 5, 6}));      // P4: P2 P3 [P4] P5 P6 + leader
        CHECK((positions(4) == std::vector<int>{1, 3, 4, 5, 6, 7}));      // P5
        CHECK((positions(18) == std::vector<int>{1, 16, 17, 18, 19, 20})); // P19: only one behind
        for (int me : {0, 4, 9, 18, 19}) CHECK(positions(me).size() == 6);
        r.playerIdx = 200; // not classified
        CHECK(r.compactOrder().size() == 6);
        for (int i = 4; i < 20; i++) r.cars[i].active = false; // only four cars
        r.playerIdx = 2;
        CHECK(r.compactOrder().size() == 4);
    }

    // Gaps relative to the player: ahead negative, behind positive, self +0.000.
    {
        Snapshot r;
        r.playerIdx = 2;
        const uint32_t toLeader[4] = {0, 1500, 3200, 4100};
        for (int i = 0; i < 4; i++) {
            auto &c = r.cars[i];
            c.active = true;
            c.position = uint8_t(i + 1);
            c.lapNum = 10;
            c.gapToLeaderMs = toLeader[i];
        }
        CHECK(relativeToPlayer(r, 0).ms == -3200);
        CHECK(relativeToPlayer(r, 1).ms == -1700);
        CHECK(relativeToPlayer(r, 2).valid && relativeToPlayer(r, 2).ms == 0);
        CHECK(relativeToPlayer(r, 3).ms == 900 && relativeToPlayer(r, 3).laps == 0);
        r.cars[3].lapNum = 9; // lapped
        CHECK(relativeToPlayer(r, 3).laps == 1);
        CHECK(formatSignedGap(-1208) == "-1.208");
        CHECK(formatSignedGap(0) == "+0.000");
        CHECK(formatSignedGap(842) == "+0.842");
        r.cars[2].position = 0; // player not classified
        CHECK(!relativeToPlayer(r, 0).valid);
    }

    CHECK(formatLap(91482) == "1:31.482");
    CHECK(formatSector(31482) == "31.482");
    CHECK(formatGap(1208) == "+1.208");

    // Names: ASCII surname, Chinese name by driver id, unknown -> race number
    {
        Car c;
        c.ai = true;
        c.name = "Lando NORRIS";
        CHECK(driverCode(c) == "NOR");
        c.name = "\xe8\xaf\xba\xe9\x87\x8c\xe6\x96\xaf"; // 诺里斯
        c.driverId = 54;
        CHECK(driverCode(c) == "NOR");
        c.driverId = 250;
        c.raceNumber = 69;
        CHECK(driverCode(c) == "#69");
        c.raceNumber = 22;
        CHECK(driverCode(c) == "TSU");
    }
    // Human players: the first Latin word of the name
    {
        Car c;
        c.name = "LytonÂ·Yao"; // Lyton·Yao
        CHECK(driverCode(c) == "LTN");
        CHECK(playerCode("ç©å®¶") == "PLAYER"); // 玩家
        CHECK(playerCode("Player") == "PLR");
        CHECK(playerCode("LiTongQAQ") == "LTN");
        CHECK(playerCode("Ava") == "AVA");
    }
    // Practice / qualifying gaps: best-lap differences
    {
        Snapshot q;
        q.session.valid = true;
        q.session.sessionType = 1; // practice 1
        CHECK(rankedOnBestLap(q));
        for (int i = 0; i < 3; i++) { q.cars[i].active = true; q.cars[i].position = uint8_t(i + 1); }
        q.cars[0].bestLapMs = 80000;
        q.cars[1].bestLapMs = 80312;
        q.playerIdx = 1;
        CHECK(bestLapGap(q, 0, 1).valid && bestLapGap(q, 0, 1).ms == -312);
        CHECK(!bestLapGap(q, 2, 1).valid); // no time yet
        CHECK(carAhead(q, 2) == 1 && carAhead(q, 0) == -1);
        q.session.sessionType = 15; // race
        CHECK(!rankedOnBestLap(q));
    }

    // Car setups: the player's car (wheel order reordered RL RR FL FR -> as in Inputs, which is the same).
    {
        f1tel::PacketCarSetups p{};
        std::memcpy(&p.header, hist.data(), sizeof p.header);
        p.header.packetId = uint8_t(f1tel::PacketId::CarSetups);
        auto &c = p.car[p.header.playerCarIndex];
        c.frontWing = 23; c.rearWing = 18; c.onThrottle = 55; c.brakeBias = 56; c.frontCamber = -3.1f;
        c.tyrePressure = {21.5f, 21.6f, 22.9f, 23.0f};
        c.fuelLoad = 18.4f;
        p.nextFrontWingValue = 25.f;
        std::vector<uint8_t> b(sizeof p);
        std::memcpy(b.data(), &p, sizeof p);
        feed(rs, b);
        const auto &st = rs.snapshot().setup;
        CHECK(st.valid && st.frontWing == 23 && st.rearWing == 18 && st.onThrottle == 55 && st.brakeBias == 56);
        CHECK(std::fabs(st.frontCamber + 3.1f) < 1e-4f && std::fabs(st.fuelLoad - 18.4f) < 1e-4f);
        CHECK(std::fabs(st.tyrePressure[0] - 21.5f) < 1e-4f && std::fabs(st.tyrePressure[3] - 23.0f) < 1e-4f);
        CHECK(std::fabs(st.nextFrontWing - 25.f) < 1e-4f);
    }

    // A packet with sessionUID 0 (the game sends some when leaving a session) leaves the state alone.
    {
        const size_t lapsBefore = rs.snapshot().cars[19].laps.size();
        auto zero = hist;
        std::memset(zero.data() + 7, 0, 8); // header.sessionUID
        feed(rs, zero);
        CHECK(rs.snapshot().cars[19].laps.size() == lapsBefore && lapsBefore > 0);
    }

    // A new session UID wipes the state.
    auto other = parts;
    other[13] ^= 0xFF;
    feed(rs, other);
    s = rs.snapshot();
    CHECK(s.sessionBestSectorMs[0] == 0);
    CHECK(s.cars[19].laps.empty());

    std::printf(failures ? "%d FAILED\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
