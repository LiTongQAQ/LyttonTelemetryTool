#include "race_state.hpp"

#include <f1tel/parse.hpp>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>

namespace timing {

using namespace f1tel;

std::vector<int> Snapshot::order() const
{
    std::vector<int> o;
    for (int i = 0; i < MAX_CARS; i++)
        if (cars[i].active && cars[i].position > 0) o.push_back(i);
    std::sort(o.begin(), o.end(), [&](int a, int b) { return cars[a].position < cars[b].position; });
    return o;
}

std::vector<int> Snapshot::compactOrder(int rows) const
{
    const auto all = order();
    const int n = int(all.size());
    if (n <= rows) return all;
    int m = -1; // player's index in the order
    for (int k = 0; k < n; k++)
        if (all[k] == playerIdx) m = k;
    auto range = [&](int from) { return std::vector<int>(all.begin() + from, all.begin() + from + rows); };
    if (m < 0) return range(0);                      // player not classified: the top of the order
    const int behind = 2, ahead = rows - 2 - behind; // 2 ahead + you + 2 behind, plus the leader
    if (m - ahead <= 1) return range(0);             // leading, or the window reaches the leader: top places
    if (m == n - 1) return range(n - rows);          // last: the five ahead + you
    if (m + behind >= n) {                           // second to last: leader + the last rows-1
        std::vector<int> v{all[0]};
        v.insert(v.end(), all.end() - (rows - 1), all.end());
        return v;
    }
    std::vector<int> v{all[0]};
    for (int k = m - ahead; k <= m + behind; k++) v.push_back(all[k]);
    return v;
}

int lapsDown(const Snapshot &s, int car)
{
    // whole laps behind the leader. Lap number alone is wrong just after the leader crosses the line (everyone
    // would be "+1 LAP" for a moment): a car is only a lap down once the leader is also further into its lap.
    if (car < 0 || car >= MAX_CARS) return 0;
    const Car *leader = nullptr;
    for (const auto &x : s.cars)
        if (x.active && x.position == 1) leader = &x;
    const auto &c = s.cars[car];
    if (!leader || &c == leader) return 0;
    int d = int(leader->lapNum) - int(c.lapNum);
    if (d > 0 && c.lapDistance > leader->lapDistance) d--;
    return std::max(d, 0);
}

RelativeGap relativeToPlayer(const Snapshot &s, int car)
{
    RelativeGap g;
    if (s.playerIdx >= MAX_CARS || car < 0 || car >= MAX_CARS) return g;
    const auto &me = s.cars[s.playerIdx];
    const auto &c = s.cars[car];
    if (!me.active || me.position == 0 || c.position == 0) return g;
    g.valid = true;
    if (car == s.playerIdx) return g;
    g.laps = lapsDown(s, car) - lapsDown(s, s.playerIdx);
    g.ms = int32_t(c.gapToLeaderMs) - int32_t(me.gapToLeaderMs);
    return g;
}

static Mark mark(uint32_t ms, bool valid, uint32_t sessionBest, uint32_t personalBest)
{
    if (ms == 0) return Mark::None;
    if (!valid) return Mark::Invalid;
    if (sessionBest && ms <= sessionBest) return Mark::SessionBest;
    if (personalBest && ms <= personalBest) return Mark::PersonalBest;
    return Mark::Slower;
}

Mark markSector(const Snapshot &s, int car, int sector, uint32_t ms, bool valid)
{
    return mark(ms, valid, s.sessionBestSectorMs[sector], s.cars[car].bestSectorMs[sector]);
}

std::array<SectorCell, 3> lapSectors(const Snapshot &s, int car, uint32_t holdMs)
{
    std::array<SectorCell, 3> out{};
    if (car < 0 || car >= int(s.cars.size())) return out;
    const Car &c = s.cars[size_t(car)];
    if (c.sector == 0 && c.currentLapMs < holdMs && !c.laps.empty() && c.laps.back().lapMs) {
        const LapRecord &l = c.laps.back();
        for (int k = 0; k < 3; k++) {
            out[size_t(k)].state = SectorCell::Held;
            out[size_t(k)].ms = l.sectorMs[size_t(k)];
            out[size_t(k)].mark = markSector(s, car, k, l.sectorMs[size_t(k)], l.lapValid && l.sectorValid[size_t(k)]);
        }
        return out;
    }
    for (int k = 0; k < 3; k++) {
        SectorCell &o = out[size_t(k)];
        if (k < int(c.sector) && k < 2) {
            o.state = SectorCell::Done;
            o.ms = c.currentSectorMs[size_t(k)];
            o.mark = markSector(s, car, k, o.ms, !c.currentLapInvalid);
        } else if (k == int(c.sector)) {
            o.state = SectorCell::Running;
        }
    }
    return out;
}

const char *runningLabel(const Car &c)
{
    if (c.pitStatus != 0 || c.driverStatus == 0) return "PIT";
    if (c.driverStatus == 2) return "IN LAP";
    if (c.driverStatus == 3) return "OUT LAP";
    return "LIVE";
}

Mark markLap(const Snapshot &s, int car, uint32_t ms, bool valid)
{
    return mark(ms, valid, s.sessionBestLapMs, s.cars[car].bestLapMs);
}

void RaceState::feed(const uint8_t *data, size_t len)
{
    auto h = read_header(data, len);
    // sessionUID 0: not in a session (the game sends a few of these when leaving one); they must not start over
    if (!h || !h.header.sessionUID) return;
    std::lock_guard<std::mutex> l(m_);

    if (h.header.sessionUID != s_.sessionUID) { // new session: start over
        s_ = Snapshot{};
        s_.sessionUID = h.header.sessionUID;
        resetFuel();
    }
    const float now = h.header.sessionTime;
    s_.hasData = true;
    s_.playerIdx = h.header.playerCarIndex;
    s_.session.clock = now;

    switch (PacketId(h.header.packetId)) {
    case PacketId::Participants: {
        auto p = parse<PacketParticipants>(data, len);
        if (!p) break;
        s_.numActive = p->numActiveCars;
        for (int i = 0; i < MAX_CARS; i++) {
            auto &c = s_.cars[i];
            const auto &d = p->car[i];
            char name[33];
            std::memcpy(name, d.name, 32);
            name[32] = 0;
            // a slot with a driver in it: numActiveCars is a count, not the first N slots (when a car retires it drops
            // by one while the player may sit in the last slot)
            c.active = name[0] != 0 || i < p->numActiveCars;
            c.name = name;
            c.teamId = d.teamId;
            c.raceNumber = d.raceNumber;
            c.driverId = d.driverId;
            c.ai = d.aiControlled != 0;
            c.you = i == h.header.playerCarIndex;
            c.telemetryPublic = d.yourTelemetry != 0;
        }
        break;
    }
    case PacketId::Motion: {
        auto p = parse<PacketMotion>(data, len);
        if (!p) break;
        for (int i = 0; i < MAX_CARS; i++) {
            s_.cars[i].worldX = p->car[i].worldPositionX;
            s_.cars[i].worldZ = p->car[i].worldPositionZ;
            const float fx = p->car[i].worldForwardDirX / 32767.f, fz = p->car[i].worldForwardDirZ / 32767.f;
            if (fx * fx + fz * fz > 0.01f) { s_.cars[i].fwdX = fx; s_.cars[i].fwdZ = fz; }
            s_.cars[i].hasPosition = true;
        }
        if (h.header.playerCarIndex < MAX_CARS) {
            const auto &m = p->car[h.header.playerCarIndex];
            auto &c = s_.chassis;
            c.valid = true;
            c.gLat = m.gForceLateral;
            c.gLong = m.gForceLongitudinal;
            c.gVert = m.gForceVertical;
            c.pitch = m.pitch;
            c.roll = m.roll;
        }
        break;
    }
    case PacketId::MotionEx: {
        auto p = parse<PacketMotionEx>(data, len);
        if (!p) break;
        auto &w = s_.wheels;
        w.valid = true;
        auto copy = [](std::array<float, 4> &o, const Wheels<float> &v) { o = {v.rl, v.rr, v.fl, v.fr}; };
        copy(w.load, p->wheelVertForce);
        copy(w.slipRatio, p->wheelSlipRatio);
        copy(w.slipAngle, p->wheelSlipAngle);
        copy(w.latForce, p->wheelLatForce);
        copy(w.longForce, p->wheelLongForce);
        auto &c = s_.chassis;
        c.exValid = true;
        c.cogHeight = p->heightOfCOGAboveGround;
        c.frontAeroHeight = p->frontAeroHeight;
        c.rearAeroHeight = p->rearAeroHeight;
        c.frontRoll = p->frontRollAngle;
        c.rearRoll = p->rearRollAngle;
        c.chassisPitch = p->chassisPitch;
        c.frontWheelsAngle = p->frontWheelsAngle;
        c.localVelocity = {p->localVelocityX, p->localVelocityY, p->localVelocityZ};
        c.angularVelocity = {p->angularVelocityX, p->angularVelocityY, p->angularVelocityZ};
        copy(c.suspensionPos, p->suspensionPosition);
        copy(c.suspensionVel, p->suspensionVelocity);
        copy(c.wheelSpeed, p->wheelSpeed);
        copy(c.camber, p->wheelCamber);
        break;
    }
    case PacketId::Session: {
        auto p = parse<PacketSession>(data, len);
        if (!p) break;
        auto &si = s_.session;
        si.valid = true;
        si.trackId = p->trackId;
        si.trackLength = p->trackLength;
        si.totalLaps = p->totalLaps;
        si.sessionType = p->sessionType;
        si.weather = p->weather;
        si.safetyCar = p->safetyCarStatus;
        si.trackTemp = p->trackTemperature;
        si.airTemp = p->airTemperature;
        si.sector2Start = p->sector2LapDistanceStart;
        si.sector3Start = p->sector3LapDistanceStart;
        if (p->sessionTimeLeft != si.timeLeft) si.timeLeftAt = now;
        si.timeLeft = p->sessionTimeLeft;
        si.duration = p->sessionDuration;
        si.pitSpeedLimit = p->pitSpeedLimit;
        si.pitIdealLap = p->pitStopWindowIdealLap;
        si.pitLatestLap = p->pitStopWindowLatestLap;
        si.pitRejoinPosition = p->pitStopRejoinPosition;
        si.forecastAccuracy = p->forecastAccuracy;
        si.forecast.clear();
        const int n = std::min<int>(p->numWeatherForecastSamples, MAX_WEATHER_SAMPLES);
        for (int k = 0; k < n; k++) { // the game sends samples for every session of the weekend
            const auto &w = p->weatherForecastSamples[k];
            if (w.sessionType != p->sessionType) continue;
            si.forecast.push_back({w.timeOffset, w.weather, w.rainPercentage, w.trackTemperature, w.airTemperature});
        }
        std::stable_sort(si.forecast.begin(), si.forecast.end(),
                         [](const Forecast &a, const Forecast &b) { return a.minutes < b.minutes; });
        si.assists = {p->steeringAssist, p->brakingAssist, p->gearboxAssist, p->pitAssist, p->pitReleaseAssist,
                      p->ersAssist,      p->drsAssist,     p->dynamicRacingLine, p->dynamicRacingLineType};
        break;
    }
    case PacketId::LapData: {
        auto p = parse<PacketLapData>(data, len);
        if (!p) break;
        if (h.header.playerCarIndex < MAX_CARS) { // fuel per lap, track-limit counters
            const auto &me = p->car[h.header.playerCarIndex];
            s_.player.cornerCuttingWarnings = me.cornerCuttingWarnings;
            s_.player.totalWarnings = me.totalWarnings;
            s_.player.penaltiesS = me.penalties;
            const int lap = me.currentLapNum;
            auto &f = s_.fuel;
            if (lap != fuelLap_ && s_.player.fuelKg > 0) {
                if (lapStartFuel_ >= 0 && lap == fuelLap_ + 1) {
                    f.lastLapKg = std::max(0.f, lapStartFuel_ - s_.player.fuelKg);
                    if (f.lastLapKg > 0.05f) {
                        fuelSum_ += f.lastLapKg;
                        f.lapsCounted++;
                        f.avgLapKg = fuelSum_ / float(f.lapsCounted);
                    }
                }
                lapStartFuel_ = s_.player.fuelKg;
                fuelLap_ = lap;
                f.lapUsedKg = 0;
            }
        }
        for (int i = 0; i < MAX_CARS; i++) {
            auto &c = s_.cars[i];
            const auto &d = p->car[i];
            if (d.lapDistance == 0 && d.carPosition == 0) continue; // empty slot
            if (!s_.numActive) c.active = d.resultStatus >= 2; // no Participants yet
            if (d.currentLapNum != c.lapNum) c.currentSectorMs = {}; // new lap
            c.position = d.carPosition;
            c.lapNum = d.currentLapNum;
            c.sector = d.sector;
            c.currentLapMs = d.currentLapTimeInMS;
            c.currentLapInvalid = d.currentLapInvalid != 0;
            c.lastLapMs = d.lastLapTimeInMS;
            if (d.sector >= 1) c.currentSectorMs[0] = split_ms(d.sector1TimeMinutesPart, d.sector1TimeMSPart);
            if (d.sector >= 2) c.currentSectorMs[1] = split_ms(d.sector2TimeMinutesPart, d.sector2TimeMSPart);
            c.gapToCarAheadMs = split_ms(d.deltaToCarInFrontMinutesPart, d.deltaToCarInFrontMSPart);
            c.gapToLeaderMs = split_ms(d.deltaToRaceLeaderMinutesPart, d.deltaToRaceLeaderMSPart);
            c.pitStatus = d.pitStatus;
            c.driverStatus = d.driverStatus;
            c.resultStatus = d.resultStatus;
            c.lapDistance = d.lapDistance;
            c.numPitStops = d.numPitStops;
        }
        break;
    }
    case PacketId::SessionHistory: {
        auto p = parse<PacketSessionHistory>(data, len);
        if (!p || p->carIdx >= MAX_CARS) break;
        auto &c = s_.cars[p->carIdx];
        int n = std::min<int>(p->numLaps, MAX_LAPS_IN_HISTORY);
        c.laps.clear();
        for (int k = 0; k < n; k++) {
            const auto &d = p->lap[k];
            LapRecord r;
            r.lapMs = d.lapTimeInMS;
            r.sectorMs = {split_ms(d.sector1TimeMinutesPart, d.sector1TimeMSPart),
                          split_ms(d.sector2TimeMinutesPart, d.sector2TimeMSPart),
                          split_ms(d.sector3TimeMinutesPart, d.sector3TimeMSPart)};
            r.lapValid = d.lapValidBitFlags & 1;
            r.sectorValid = {bool(d.lapValidBitFlags & 2), bool(d.lapValidBitFlags & 4), bool(d.lapValidBitFlags & 8)};
            if (r.lapMs == 0 && r.sectorMs[0] == 0) continue; // the running lap is listed with zeros
            c.laps.push_back(r);
        }
        auto at = [&](uint8_t lapNum) -> const LapRecord * {
            return lapNum >= 1 && lapNum <= c.laps.size() ? &c.laps[lapNum - 1] : nullptr;
        };
        c.bestLapMs = at(p->bestLapTimeLapNum) ? at(p->bestLapTimeLapNum)->lapMs : 0;
        const uint8_t bestNums[3] = {p->bestSector1LapNum, p->bestSector2LapNum, p->bestSector3LapNum};
        for (int k = 0; k < 3; k++) c.bestSectorMs[k] = at(bestNums[k]) ? at(bestNums[k])->sectorMs[k] : 0;
        recomputeBests();
        break;
    }
    case PacketId::CarStatus: {
        auto p = parse<PacketCarStatus>(data, len);
        if (!p) break;
        for (int i = 0; i < MAX_CARS; i++) {
            s_.cars[i].visualTyre = p->car[i].visualTyreCompound;
            s_.cars[i].tyreAgeLaps = p->car[i].tyresAgeLaps;
        }
        if (h.header.playerCarIndex < MAX_CARS) {
            const auto &c = p->car[h.header.playerCarIndex];
            s_.player.fuelKg = c.fuelInTank;
            s_.player.fuelLaps = c.fuelRemainingLaps;
            s_.player.ersJ = c.ersStoreEnergy;
            s_.player.ersMode = c.ersDeployMode;
            s_.player.drsAllowed = c.drsAllowed;
            auto &cf = s_.config;
            cf.valid = true;
            cf.tractionControl = c.tractionControl;
            cf.abs = c.antiLockBrakes;
            cf.fuelMix = c.fuelMix;
            cf.brakeBias = c.frontBrakeBias;
            cf.pitLimiter = c.pitLimiterStatus;
            cf.fuelCapacity = c.fuelCapacity;
            cf.maxRPM = c.maxRPM;
            cf.idleRPM = c.idleRPM;
            cf.maxGears = c.maxGears;
            cf.actualTyre = c.actualTyreCompound;
            cf.visualTyre = c.visualTyreCompound;
            cf.tyreAge = c.tyresAgeLaps;
            cf.fiaFlag = c.vehicleFiaFlags;
            cf.iceKW = c.enginePowerICE / 1000.f;
            cf.mgukKW = c.enginePowerMGUK / 1000.f;

            // fuel flow: slope of fuelInTank over the last second or so
            auto &f = s_.fuel;
            fuelHist_.push_back({now, c.fuelInTank});
            while (!fuelHist_.empty() && (fuelHist_.front().first < now - 1.5f || fuelHist_.front().first > now))
                fuelHist_.pop_front();
            const float span = fuelHist_.empty() ? 0.f : fuelHist_.back().first - fuelHist_.front().first;
            if (span >= 0.5f) {
                const float rate = std::max(0.f, (fuelHist_.front().second - fuelHist_.back().second) / span);
                f.kgPerS += (rate - f.kgPerS) * 0.3f;
            }
            const float ms = s_.player.speed / 3.6f;
            f.kgPer100km = ms > 5.f ? f.kgPerS / ms * 100000.f : 0.f;
            if (lapStartFuel_ >= 0) f.lapUsedKg = std::max(0.f, lapStartFuel_ - c.fuelInTank);
        }
        break;
    }
    case PacketId::CarDamage: {
        auto p = parse<PacketCarDamage>(data, len);
        if (!p || h.header.playerCarIndex >= MAX_CARS) break;
        const auto &c = p->car[h.header.playerCarIndex];
        const auto &w = c.tyresWear;
        s_.player.tyreWear = {w.rl, w.rr, w.fl, w.fr};
        Damage d;
        d.valid = true;
        d.tyreDamage = {c.tyresDamage.rl, c.tyresDamage.rr, c.tyresDamage.fl, c.tyresDamage.fr};
        d.brakeDamage = {c.brakesDamage.rl, c.brakesDamage.rr, c.brakesDamage.fl, c.brakesDamage.fr};
        d.blisters = {c.tyreBlisters.rl, c.tyreBlisters.rr, c.tyreBlisters.fl, c.tyreBlisters.fr};
        d.frontWingL = c.frontLeftWingDamage;
        d.frontWingR = c.frontRightWingDamage;
        d.rearWing = c.rearWingDamage;
        d.floor = c.floorDamage;
        d.diffuser = c.diffuserDamage;
        d.sidepod = c.sidepodDamage;
        d.gearbox = c.gearBoxDamage;
        d.engine = c.engineDamage;
        d.ice = c.engineICEWear;
        d.mguh = c.engineMGUHWear;
        d.mguk = c.engineMGUKWear;
        d.es = c.engineESWear;
        d.ce = c.engineCEWear;
        d.tc = c.engineTCWear;
        d.drsFault = c.drsFault != 0;
        d.ersFault = c.ersFault != 0;
        d.engineBlown = c.engineBlown != 0;
        d.engineSeized = c.engineSeized != 0;
        if (s_.damage.valid) damageEvents(s_.damage, d, now);
        s_.damage = d;
        break;
    }
    case PacketId::CarSetups: {
        auto p = parse<PacketCarSetups>(data, len);
        if (!p || h.header.playerCarIndex >= MAX_CARS) break;
        const auto &c = p->car[h.header.playerCarIndex];
        auto &st = s_.setup;
        st.valid = true;
        st.frontWing = c.frontWing;
        st.rearWing = c.rearWing;
        st.onThrottle = c.onThrottle;
        st.offThrottle = c.offThrottle;
        st.frontCamber = c.frontCamber;
        st.rearCamber = c.rearCamber;
        st.frontToe = c.frontToe;
        st.rearToe = c.rearToe;
        st.frontSuspension = c.frontSuspension;
        st.rearSuspension = c.rearSuspension;
        st.frontAntiRollBar = c.frontAntiRollBar;
        st.rearAntiRollBar = c.rearAntiRollBar;
        st.frontRideHeight = c.frontSuspensionHeight;
        st.rearRideHeight = c.rearSuspensionHeight;
        st.brakePressure = c.brakePressure;
        st.brakeBias = c.brakeBias;
        st.engineBraking = c.engineBraking;
        st.tyrePressure = {c.tyrePressure.rl, c.tyrePressure.rr, c.tyrePressure.fl, c.tyrePressure.fr};
        st.ballast = c.ballast;
        st.fuelLoad = c.fuelLoad;
        st.nextFrontWing = p->nextFrontWingValue;
        break;
    }
    case PacketId::CarTelemetry: {
        auto p = parse<PacketCarTelemetry>(data, len);
        if (!p || h.header.playerCarIndex >= MAX_CARS) break;
        const auto &t = p->car[h.header.playerCarIndex];
        auto &pl = s_.player;
        pl.throttle = t.throttle;
        pl.brake = t.brake;
        pl.steer = t.steer;
        pl.speed = t.speed;
        pl.gear = t.gear;
        pl.rpm = t.engineRPM;
        pl.clutch = t.clutch;
        pl.revLights = t.revLightsPercent;
        pl.drs = t.drs;
        pl.engineTemp = t.engineTemperature;
        pl.tyreSurface = {t.tyresSurfaceTemperature.rl, t.tyresSurfaceTemperature.rr, t.tyresSurfaceTemperature.fl,
                          t.tyresSurfaceTemperature.fr};
        pl.tyreInner = {t.tyresInnerTemperature.rl, t.tyresInnerTemperature.rr, t.tyresInnerTemperature.fl,
                        t.tyresInnerTemperature.fr};
        pl.brakeTemp = {t.brakesTemperature.rl, t.brakesTemperature.rr, t.brakesTemperature.fl, t.brakesTemperature.fr};
        pl.tyrePressure = {t.tyresPressure.rl, t.tyresPressure.rr, t.tyresPressure.fl, t.tyresPressure.fr};
        pl.surface = {t.surfaceType.rl, t.surfaceType.rr, t.surfaceType.fl, t.surfaceType.fr};
        break;
    }
    default: break;
    }
}

void RaceState::resetFuel()
{
    fuelHist_.clear();
    lapStartFuel_ = -1;
    fuelSum_ = 0;
    fuelLap_ = 0;
}

void RaceState::damageEvents(const Damage &a, const Damage &b, float t)
{
    const int lap = s_.playerIdx < MAX_CARS ? s_.cars[s_.playerIdx].lapNum : 0;
    auto add = [&](int level, std::string text) {
        s_.events.push_back({t, lap, level, std::move(text)});
        if (s_.events.size() > 40) s_.events.erase(s_.events.begin());
    };
    auto flag = [&](bool was, bool is, const char *name, const char *fault, const char *ok) {
        if (!was && is) add(2, std::string(name) + fault);
        if (was && !is) add(0, std::string(name) + ok);
    };
    flag(a.drsFault, b.drsFault, "DRS ", "fault", "recovered");
    flag(a.ersFault, b.ersFault, "ERS ", "fault", "recovered");
    flag(a.engineBlown, b.engineBlown, "Engine", "ENGINE BLOWN", "recovered");
    flag(a.engineSeized, b.engineSeized, "Engine", "ENGINE SEIZED", "recovered");
    auto part = [&](uint8_t was, uint8_t is, const char *name) {
        char buf[96];
        if (is >= was + 5) {
            std::snprintf(buf, sizeof buf, "%s damaged %u%%", name, unsigned(is));
            add(1, buf);
        } else if (is + 5 <= was) {
            std::snprintf(buf, sizeof buf, "%s repaired (%u%% -> %u%%)", name, unsigned(was), unsigned(is));
            add(0, buf);
        }
    };
    part(a.frontWingL, b.frontWingL, "Front wing left");
    part(a.frontWingR, b.frontWingR, "Front wing right");
    part(a.rearWing, b.rearWing, "Rear wing");
    part(a.floor, b.floor, "Floor");
    part(a.diffuser, b.diffuser, "Diffuser");
    part(a.sidepod, b.sidepod, "Sidepods");
    part(a.gearbox, b.gearbox, "Gearbox");
    part(a.engine, b.engine, "Engine");
}

void RaceState::recomputeBests()
{
    s_.sessionBestSectorMs = {};
    s_.sessionBestLapMs = 0;
    auto take = [](uint32_t &best, uint32_t v) {
        if (v && (!best || v < best)) best = v;
    };
    for (const auto &c : s_.cars) {
        for (const auto &r : c.laps) {
            if (r.lapValid) take(s_.sessionBestLapMs, r.lapMs);
            for (int k = 0; k < 3; k++)
                if (r.sectorValid[k]) take(s_.sessionBestSectorMs[k], r.sectorMs[k]);
        }
    }
}

void RaceState::reset()
{
    std::lock_guard<std::mutex> l(m_);
    s_ = Snapshot{};
    resetFuel();
}

Snapshot RaceState::snapshot() const
{
    std::lock_guard<std::mutex> l(m_);
    return s_;
}

SessionGroup sessionGroup(int t)
{
    if (t >= 1 && t <= 4) return SessionGroup::Practice;
    if (t >= 5 && t <= 9) return SessionGroup::Qualifying;
    if (t >= 10 && t <= 14) return SessionGroup::SprintShootout;
    if (t == 15 || t == 17) return SessionGroup::Race;
    if (t == 16) return SessionGroup::Sprint;
    if (t == 18) return SessionGroup::TimeTrial;
    return SessionGroup::Unknown;
}

const char *sessionTypeName(int t)
{
    static const char *n[] = {"Unknown",          "Practice 1",       "Practice 2",       "Practice 3",     "Short Practice",
                              "Qualifying Q1",     "Qualifying Q2",      "Qualifying Q3",      "Short Qualifying",     "One-Shot Qualifying",
                              "Sprint Shootout SQ1",  "Sprint Shootout SQ2",   "Sprint Shootout SQ3",   "Short Sprint Shootout", "One-Shot Sprint Shootout",
                              "Race",          "Sprint",         "Race 3",         "Time Trial"};
    return t >= 0 && t <= 18 ? n[t] : "Unknown";
}

const char *sessionGroupName(SessionGroup g)
{
    switch (g) {
    case SessionGroup::Practice: return "Practice";
    case SessionGroup::Qualifying: return "Qualifying";
    case SessionGroup::SprintShootout: return "Sprint Shootout";
    case SessionGroup::Race: return "Race";
    case SessionGroup::Sprint: return "Sprint";
    case SessionGroup::TimeTrial: return "Time Trial";
    default: return "Unknown";
    }
}

const char *sessionTypeTag(int t)
{
    static const char *n[] = {"Session", "FP1", "FP2", "FP3", "ShortFP", "Q1",   "Q2",    "Q3",  "ShortQ", "OneShotQ",
                              "SQ1",     "SQ2", "SQ3", "ShortSQ", "OneShotSQ", "Race", "Sprint", "Race3", "TT"};
    return t >= 0 && t <= 18 ? n[t] : "Session";
}

std::string formatLap(uint32_t ms)
{
    if (!ms) return "-:--.---";
    char b[24];
    std::snprintf(b, sizeof b, "%u:%02u.%03u", ms / 60000, ms / 1000 % 60, ms % 1000);
    return b;
}

std::string formatSector(uint32_t ms)
{
    if (!ms) return "--.---";
    char b[24];
    if (ms >= 60000) return formatLap(ms);
    std::snprintf(b, sizeof b, "%u.%03u", ms / 1000, ms % 1000);
    return b;
}

std::string formatGap(uint32_t ms)
{
    char b[24];
    std::snprintf(b, sizeof b, "+%u.%03u", ms / 1000, ms % 1000);
    return b;
}

std::string formatSignedGap(int32_t ms)
{
    const uint32_t a = uint32_t(ms < 0 ? -ms : ms);
    char b[24];
    std::snprintf(b, sizeof b, "%c%u.%03u", ms < 0 ? '-' : '+', a / 1000, a % 1000);
    return b;
}

bool rankedOnBestLap(const Snapshot &s)
{
    if (!s.session.valid) return false;
    const auto g = sessionGroup(s.session.sessionType);
    return g == SessionGroup::Practice || g == SessionGroup::Qualifying || g == SessionGroup::SprintShootout ||
           g == SessionGroup::TimeTrial;
}

LapGap bestLapGap(const Snapshot &s, int car, int ref)
{
    LapGap g;
    if (car < 0 || ref < 0 || car >= MAX_CARS || ref >= MAX_CARS) return g;
    const uint32_t a = s.cars[car].bestLapMs, b = s.cars[ref].bestLapMs;
    if (!a || !b) return g;
    g.ms = int32_t(a) - int32_t(b);
    g.valid = true;
    return g;
}

int carAhead(const Snapshot &s, int car)
{
    const int p = s.cars[car].position;
    if (p <= 1) return -1;
    for (int i = 0; i < MAX_CARS; i++)
        if (s.cars[i].active && s.cars[i].position == p - 1) return i;
    return -1;
}

namespace {
std::mutex g_codeMx;
std::string g_myCode;
} // namespace

void setMyCode(const std::string &code)
{
    std::lock_guard<std::mutex> l(g_codeMx);
    g_myCode = code;
}

std::string driverCode(const Car &c)
{
    if (c.you) {
        std::lock_guard<std::mutex> l(g_codeMx);
        if (!g_myCode.empty()) return g_myCode;
    }
    bool ascii = !c.name.empty();
    for (unsigned char ch : c.name) ascii &= ch < 0x80;
    if (ascii) return shortName(c.name);
    if (!c.ai) return playerCode(c.name);
    // F1 25 driver ids, checked against the (Chinese) names in a real recording of 2026-11-06
    static const std::map<int, const char *> codes = {
        {0, "SAI"},   {3, "ALO"},   {7, "HAM"},   {9, "VER"},   {10, "HUL"},  {17, "OCO"},  {19, "STR"},
        {50, "RUS"},  {54, "NOR"},  {58, "LEC"},  {59, "GAS"},  {62, "ALB"},  {112, "PIA"}, {113, "LAW"},
        {147, "BEA"}, {149, "HAD"}, {161, "BOR"}, {162, "COL"}, {165, "ANT"}};
    const auto it = codes.find(c.driverId);
    if (it != codes.end()) return it->second;
    // ids not seen yet: the 2025 grid by race number
    static const std::map<int, const char *> byNumber = {
        {1, "VER"},  {4, "NOR"},  {5, "BOR"},  {6, "HAD"},  {10, "GAS"}, {12, "ANT"}, {14, "ALO"},
        {16, "LEC"}, {18, "STR"}, {22, "TSU"}, {23, "ALB"}, {27, "HUL"}, {30, "LAW"}, {31, "OCO"},
        {43, "COL"}, {44, "HAM"}, {55, "SAI"}, {63, "RUS"}, {81, "PIA"}, {87, "BEA"}};
    const auto nt = byNumber.find(c.raceNumber);
    if (nt != byNumber.end()) return nt->second;
    char b[8];
    std::snprintf(b, sizeof b, "#%u", unsigned(c.raceNumber));
    return b;
}

// A human player's name as a code from the first word in Latin letters (words split by spaces, dots, the middle dot,
// underscores): its first letter, then its next consonants, filled up with its other letters ("Lyton·Yao" -> LTN,
// "LiTongQAQ" -> LTN); PLAYER when there is none.
std::string playerCode(const std::string &name)
{
    std::string word;
    for (size_t i = 0; i <= name.size(); i++) {
        const unsigned char ch = i < name.size() ? (unsigned char)name[i] : ' ';
        if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z')) {
            word += char(ch >= 'a' ? ch - 32 : ch);
            continue;
        }
        if (!word.empty()) break;
    }
    if (word.empty()) return "PLAYER";
    std::string code(1, word[0]);
    auto vowel = [](char ch) { return std::string("AEIOUY").find(ch) != std::string::npos; };
    std::vector<bool> used(word.size(), false);
    used[0] = true;
    for (size_t i = 1; i < word.size() && code.size() < 3; i++)
        if (!vowel(word[i])) { code += word[i]; used[i] = true; }
    for (size_t i = 1; i < word.size() && code.size() < 3; i++)
        if (!used[i]) code += word[i];
    return code;
}

std::string shortName(const std::string &name)
{
    std::string up;
    for (char ch : name) {
        if (ch == ' ') { up.clear(); continue; } // last word: surname
        up += char(ch >= 'a' && ch <= 'z' ? ch - 32 : ch);
    }
    if (up.empty()) up = name;
    return up.substr(0, 3);
}

} // namespace timing
