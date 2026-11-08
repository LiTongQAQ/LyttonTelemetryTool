// Interactive replay of a .f1tr recording: play, pause, seek, change speed.
//
// The whole file is loaded into memory and indexed. Packets go to `sink` (normally Hub::inject, so subscribers and
// forward targets see them like live data). Seeking backwards calls `reset` first and then re-feeds every packet
// from the start up to the new position at full speed, so state built from the stream (standings, lap history) is
// exactly what it was at that moment.
#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace f1hub {

struct RecordingInfo {
    bool ok = false;
    uint64_t durationNs = 0;
    uint64_t packets = 0;
    uint64_t bytes = 0;
    int trackId = -1, sessionType = -1; // from the first Session packet, -1 = none
};

// Walks the record headers only (no payloads): quick enough for a file list.
RecordingInfo scanRecording(const std::string &path);

struct LapMark {
    uint64_t tNs; // when the player started this lap
    int lapNum;
};

// What the player's car (or the session) was doing over a stretch of the recording, for the timeline.
enum class Phase : uint8_t { Garage, Pit, OutLap, InLap, Invalid, SafetyCar, VirtualSC };
struct PhaseSpan {
    uint64_t t0, t1;
    Phase kind;
};

class Replayer {
public:
    using Sink = std::function<void(const uint8_t *, size_t)>;
    Replayer(Sink sink, std::function<void()> reset);
    ~Replayer();

    bool open(const std::string &path, std::string *error = nullptr);
    void close();
    bool isOpen() const;
    const std::string &path() const { return path_; }

    void play();
    void pause();
    bool playing() const;
    bool seeking() const { return seeking_; } // true while seek() feeds the packets up to the new position
    void seek(uint64_t tNs);
    void setSpeed(double speed);
    double speed() const;

    uint64_t position() const;
    uint64_t duration() const { return duration_; }
    size_t packets() const { return index_.size(); }
    const std::vector<LapMark> &laps() const { return laps_; }
    const std::vector<PhaseSpan> &phases() const { return phases_; } // by start time; car and safety-car spans mixed

private:
    void loop();
    void emitUntil(uint64_t tNs); // caller holds m_; unlocks while calling the sink

    Sink sink_;
    std::function<void()> reset_;
    std::string path_;
    std::vector<uint8_t> buf_;
    struct Rec {
        uint64_t t;
        size_t off;
        uint16_t len;
    };
    std::vector<Rec> index_;
    std::vector<LapMark> laps_;
    std::vector<PhaseSpan> phases_;
    uint64_t duration_ = 0;

    std::mutex emit_; // one emitter at a time (playback thread or seek), taken before m_
    mutable std::mutex m_;
    std::condition_variable cv_;
    std::thread thread_;
    bool quit_ = false, playing_ = false;
    double speed_ = 1.0;
    std::atomic<bool> seeking_{false};
    size_t next_ = 0;    // next record to emit
    uint64_t pos_ = 0;   // stream time at baseWall_
    std::chrono::steady_clock::time_point baseWall_;
};

} // namespace f1hub
