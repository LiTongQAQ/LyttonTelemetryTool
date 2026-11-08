#include "trace.hpp"

#include <f1tel/parse.hpp>

#include <algorithm>
#include <cstring>

namespace timing {

using namespace f1tel;

static size_t lowerIdx(const std::vector<Sample> &s, float d)
{
    return size_t(std::lower_bound(s.begin(), s.end(), d, [](const Sample &a, float v) { return a.dist < v; }) -
                  s.begin());
}

float LapTrace::timeAt(float d) const
{
    if (s.size() < 2 || d < s.front().dist || d > s.back().dist) return -1;
    size_t i = std::max<size_t>(1, lowerIdx(s, d));
    const Sample &a = s[i - 1], &b = s[i];
    const float k = b.dist > a.dist ? (d - a.dist) / (b.dist - a.dist) : 0;
    return (a.t + (b.t - a.t) * k) - s.front().t;
}

Sample LapTrace::at(float d) const
{
    if (s.empty()) return {};
    size_t i = lowerIdx(s, d);
    if (i == 0) return s.front();
    if (i >= s.size()) return s.back();
    const Sample &a = s[i - 1], &b = s[i];
    const float k = b.dist > a.dist ? (d - a.dist) / (b.dist - a.dist) : 0;
    auto mix = [k](float x, float y) { return x + (y - x) * k; };
    Sample r;
    r.t = mix(a.t, b.t);
    r.dist = d;
    r.speed = mix(a.speed, b.speed);
    r.throttle = mix(a.throttle, b.throttle);
    r.brake = mix(a.brake, b.brake);
    r.steer = mix(a.steer, b.steer);
    r.rpm = mix(a.rpm, b.rpm);
    r.x = mix(a.x, b.x);
    r.z = mix(a.z, b.z);
    r.gear = k < 0.5f ? a.gear : b.gear;
    r.ers = k < 0.5f ? a.ers : b.ers;
    r.drs = k < 0.5f ? a.drs : b.drs;
    return r;
}

void TraceLog::mark(MarkKind k, float t)
{
    d_.marks.push_back({k, x_, z_, dist_, lapNum_, t});
    if (d_.marks.size() > 200) d_.marks.erase(d_.marks.begin());
}

// Flashbacks send the session back in time, with an FLBK event or without (the career Q2 recording of 2026-11-06 has
// seven and no FLBK): the samples, laps and marks after the new time are dropped, the lap that was running then is
// running again (as the Python Project_F1-25 did it: reopen a lap closed by a flashback over the line).
void TraceLog::rewind(float t)
{
    while (d_.laps.size() > 1 && !d_.laps.back().s.empty() && d_.laps.back().s.front().t > t) d_.laps.pop_back();
    if (!d_.laps.empty()) {
        auto &lap = d_.laps.back();
        while (!lap.s.empty() && lap.s.back().t > t) lap.s.pop_back();
        lap.lapMs = 0;
        lap.complete = false;
        lapNum_ = lap.lapNum;
    }
    d_.bestLap = -1;
    for (size_t i = 0; i + 1 < d_.laps.size(); i++) {
        const auto &l = d_.laps[i];
        if (l.complete && l.valid && l.lapMs && (d_.bestLap < 0 || l.lapMs < d_.laps[d_.bestLap].lapMs)) d_.bestLap = int(i);
    }
    while (!d_.marks.empty() && d_.marks.back().t > t) d_.marks.pop_back();
    while (!d_.recent.empty() && d_.recent.back().t > t) d_.recent.pop_back();
    cutWarn_ = totalWarn_ = -1; // the counters go back with the game
}

void TraceLog::feed(const uint8_t *data, size_t len)
{
    auto h = read_header(data, len);
    // sessionUID 0: not in a session (the game sends a few of these when leaving one); they must not start over
    if (!h || h.header.playerCarIndex >= MAX_CARS || !h.header.sessionUID) return;
    const int me = h.header.playerCarIndex;
    std::lock_guard<std::mutex> l(m_);
    if (h.header.sessionUID != session_) {
        d_ = TraceData{};
        session_ = h.header.sessionUID;
        lapNum_ = 0;
        started_ = false;
        sector_ = -1;
        cutWarn_ = totalWarn_ = -1;
        invalid_ = false;
        offWheels_ = 0;
        lastOffT_ = -10;
        ers_ = 0;
        lastT_ = -1;
    }
    {
        const float t = h.header.sessionTime;
        if (lastT_ >= 0 && t < lastT_ - 0.5f) rewind(t);
        lastT_ = t;
    }

    if (h.header.packetId == uint8_t(PacketId::Motion)) {
        if (auto p = parse<PacketMotion>(data, len)) {
            x_ = p->car[me].worldPositionX;
            z_ = p->car[me].worldPositionZ;
            d_.hasPositions = true;
        }
        return;
    }

    // the game's flashback event, when it sends one, names the time it went back to
    if (h.header.packetId == uint8_t(PacketId::Event)) {
        if (auto p = parse<PacketEvent>(data, len); p && std::memcmp(p->eventStringCode, "FLBK", 4) == 0) {
            float to;
            std::memcpy(&to, p->details + 4, 4); // frame identifier, then session time
            rewind(to);
            lastT_ = to;
        }
        return;
    }

    if (h.header.packetId == uint8_t(PacketId::CarStatus)) {
        if (auto p = parse<PacketCarStatus>(data, len)) ers_ = p->car[me].ersDeployMode;
        return;
    }

    if (h.header.packetId == uint8_t(PacketId::LapData)) {
        auto p = parse<PacketLapData>(data, len);
        if (!p) return;
        const auto &c = p->car[me];
        if (c.currentLapNum != lapNum_) {
            if (!d_.laps.empty()) {
                auto &prev = d_.laps.back();
                prev.lapMs = c.lastLapTimeInMS;
                prev.complete = started_ && c.currentLapNum == lapNum_ + 1;
                if (prev.complete && prev.valid && prev.lapMs) {
                    const int idx = int(d_.laps.size()) - 1;
                    if (d_.bestLap < 0 || prev.lapMs < d_.laps[d_.bestLap].lapMs) d_.bestLap = idx;
                }
            }
            if (d_.laps.size() >= MAX_LAPS) {
                d_.laps.erase(d_.laps.begin());
                if (d_.bestLap >= 0) d_.bestLap--;
            }
            lapNum_ = c.currentLapNum;
            started_ = c.lapDistance >= 0 && c.lapDistance < 300;
            LapTrace t;
            t.lapNum = lapNum_;
            d_.laps.push_back(std::move(t));
        }
        dist_ = c.lapDistance;
        if (sector_ >= 0 && c.sector == sector_ + 1 && c.sector <= 2) d_.sectorDist[c.sector - 1] = c.lapDistance;
        sector_ = c.sector;
        if (c.currentLapInvalid && !d_.laps.empty()) d_.laps.back().valid = false;
        // the game's own track-limit judgement
        const float t = h.header.sessionTime;
        if (cutWarn_ >= 0 && c.cornerCuttingWarnings > cutWarn_) mark(MarkKind::CornerCut, t);
        else if (totalWarn_ >= 0 && c.totalWarnings > totalWarn_) mark(MarkKind::Warning, t);
        if (c.currentLapInvalid && !invalid_ && c.cornerCuttingWarnings == cutWarn_) mark(MarkKind::LapInvalid, t);
        cutWarn_ = c.cornerCuttingWarnings;
        totalWarn_ = c.totalWarnings;
        invalid_ = c.currentLapInvalid != 0;
        d_.trackLength = std::max(d_.trackLength, dist_);
        return;
    }

    if (h.header.packetId == uint8_t(PacketId::CarTelemetry)) {
        auto p = parse<PacketCarTelemetry>(data, len);
        if (!p || d_.laps.empty()) return;
        const auto &c = p->car[me];
        Sample s;
        s.t = h.header.sessionTime;
        s.dist = dist_;
        s.speed = c.speed;
        s.throttle = c.throttle;
        s.brake = c.brake;
        s.steer = c.steer;
        s.rpm = c.engineRPM;
        s.gear = c.gear;
        s.x = x_;
        s.z = z_;
        s.ers = ers_;
        s.drs = c.drs != 0;
        // two or more wheels on grass, gravel, sand ... = off the track
        int off = 0;
        const uint8_t *surf = &c.surfaceType.rl;
        for (int w = 0; w < 4; w++) off += surf[w] >= uint8_t(Surface::Rock) && surf[w] <= uint8_t(Surface::Water);
        if (off >= 2 && offWheels_ < 2 && s.t - lastOffT_ > 1.f) {
            mark(MarkKind::OffTrack, s.t);
            lastOffT_ = s.t;
        }
        offWheels_ = off;
        auto &lap = d_.laps.back().s;
        if (dist_ >= 0 && (lap.empty() || dist_ >= lap.back().dist)) lap.push_back(s);
        while (!d_.recent.empty() && (d_.recent.front().t < s.t - RECENT_S || d_.recent.front().t > s.t))
            d_.recent.pop_front();
        d_.recent.push_back(s);
    }
}

void TraceLog::reset()
{
    std::lock_guard<std::mutex> l(m_);
    d_ = TraceData{};
    session_ = 0;
    lapNum_ = 0;
    started_ = false;
    sector_ = -1;
    cutWarn_ = totalWarn_ = -1;
    invalid_ = false;
    offWheels_ = 0;
    lastOffT_ = -10;
    lastT_ = -1;
}

void TraceLog::read(const std::function<void(const TraceData &)> &f) const
{
    std::lock_guard<std::mutex> l(m_);
    f(d_);
}

} // namespace timing
