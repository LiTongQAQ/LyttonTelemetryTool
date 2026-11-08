// Player telemetry traces: every CarTelemetry sample of the player car, filed under the lap it was driven on and
// stamped with the lap distance from the latest LapData. The analysis page plots a lap against a reference lap by
// distance; the dashboard plots the last seconds by time.
//
// feed() runs on the hub's thread. The laps can hold a lot of samples, so readers don't copy them: read() runs a
// function under the lock.
#pragma once

#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <vector>

namespace timing {

struct Sample {
    float t = 0;    // session time, s
    float dist = 0; // lap distance, m
    float speed = 0, throttle = 0, brake = 0, steer = 0;
    float rpm = 0;
    float x = 0, z = 0; // world position (Motion), for the track map
    int8_t gear = 0;
    uint8_t ers = 0;   // ERS deploy mode (CarStatus): 0 none, 1 medium, 2 hotlap, 3 overtake
    bool drs = false;  // DRS flap open
};

struct LapTrace {
    int lapNum = 0;
    uint32_t lapMs = 0;      // 0 while the lap is running or when it never finished
    bool valid = true;
    bool complete = false;   // driven from the line to the line
    std::vector<Sample> s;   // in driving order, dist ascending

    // Time into the lap at distance d (s), interpolated; negative when d is outside the trace.
    float timeAt(float d) const;
    // Sample interpolated at distance d.
    Sample at(float d) const;
};

// Where something happened to the player's car on track, for the map.
enum class MarkKind { OffTrack, CornerCut, Warning, LapInvalid };
struct TrackMark {
    MarkKind kind;
    float x, z, dist;
    int lap;
    float t;
};

struct TraceData {
    std::vector<LapTrace> laps; // oldest first; the last one is the running lap
    std::deque<Sample> recent;  // the last RECENT_S seconds
    float trackLength = 0;      // longest lap distance seen, m
    float sectorDist[2] = {0, 0}; // lap distance where S2 and S3 begin (0 = not seen yet)
    bool hasPositions = false;
    std::vector<TrackMark> marks; // newest last, at most 200
    int bestLap = -1;           // index into laps of the fastest complete valid lap
};

class TraceLog {
public:
    static constexpr float RECENT_S = 30.f;
    static constexpr size_t MAX_LAPS = 120;

    void feed(const uint8_t *data, size_t len);
    void reset();
    void read(const std::function<void(const TraceData &)> &f) const;

private:
    mutable std::mutex m_;
    TraceData d_;
    uint64_t session_ = 0;
    int lapNum_ = 0;
    float dist_ = 0;
    float x_ = 0, z_ = 0;
    uint8_t ers_ = 0;
    int sector_ = -1;
    int cutWarn_ = -1, totalWarn_ = -1;
    bool invalid_ = false;
    int offWheels_ = 0;
    float lastOffT_ = -10;
    void mark(MarkKind k, float t);
    void rewind(float t); // a flashback: forget everything after session time t
    float lastT_ = -1;    // latest session time seen
    bool started_ = false; // the current lap trace began at the line
};

} // namespace timing
