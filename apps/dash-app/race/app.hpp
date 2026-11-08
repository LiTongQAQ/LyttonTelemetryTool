// Application core of f1-race, independent of the window: where the telemetry comes from (live UDP, the virtual
// race, a recording), the hub with its forward targets, recording, and the settings file.
#pragma once

#include "hub.hpp"
#include "layout.hpp"
#include "race_state.hpp"
#include "replay.hpp"
#include "sim.hpp"
#include "trace.hpp"
#include "tracks.hpp"

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace app {

enum class Source { Live = 0, Sim = 1, Replay = 2 };

struct Settings {
    uint16_t port = 20700;
    std::vector<f1hub::Target> targets{{"127.0.0.1", 20777, true, "Tools fixed to 20777, such as Wheel Dash"}};
    int gapMode = 0; // panels::GapMode
    int startSource = 0;        // Source used at start-up (Live or Sim)
    float simSpeed = 1.f;
    float bgOpacity = 0.75f;
    float textOpacity = 1.0f;
    std::string latinFont; // font file for the Latin letters, empty = the built-in choice (takes effect on restart)
    bool latinDigits = true; // the digits from latinFont too
    std::string myCode; // your driver code in the standings (timing::setMyCode), empty = from the name
    layout::Offset offset[layout::PANELS];
    layout::Overrides hmd;
};

Settings loadSettings(const std::string &path);
void saveSettings(const Settings &s, const std::string &path);

struct RecFile {
    std::string path, name;
    f1hub::RecordingInfo info;
    std::time_t mtime = 0;
};

class App {
public:
    App(std::string dir);
    ~App();

    Settings cfg;
    std::string dir, iniPath;
    bool dirty = false; // settings changed, save soon
    std::time_t savedAt = 0; // last time the settings file was written
    void save();

    f1hub::Hub hub;
    timing::RaceState race;
    timing::TraceLog trace;
    f1hub::Replayer replay;

    Source source() const { return source_; }
    const std::string &status() const { return status_; }
    void useLive();
    void useSim();
    bool useReplay(const std::string &path);

    // recording (live and virtual sources)
    bool canRecord() const { return source_ != Source::Replay; }
    bool recording() const { return hub.recording(); }
    std::string recordingPath() const { return hub.recordingPath(); }
    double recordingSeconds() const; // recorded time, pauses excluded
    bool recordingPaused() const { return hub.recordingPaused(); }
    void startRecording();
    void stopRecording();
    const std::string &recordError() const { return recordError_; } // why the last start failed, empty = fine

    void applyTargets(); // cfg.targets -> hub
    bool relisten();     // after a port change

    // once per frame: packet rates, auto recording
    void update();
    double packetRate() const { return rate_; }
    double perIdRate(int id) const { return perIdRate_[id]; }
    double targetRate(size_t i) const { return i < targetRate_.size() ? targetRate_[i] : 0; }
    double secondsSinceData() const;

    // track map: the bundled outline aligned to the game's coordinates (see tracks.hpp)
    tracks::Geometry trackGeo;
    std::string trackStatus;

    std::vector<RecFile> recordings;
    void scanRecordings();
    std::string recordingsDir() const { return dir + "/recordings"; }

private:
    void stopAll();
    std::string newRecordingPath() const;
    void finishRecordings(); // name closed files after their session, drop empty ones
    void updateTrack();
    uint64_t trackSession_ = 0;
    int trackId_ = -2;
    bool trackFinal_ = false;
    std::chrono::steady_clock::time_point trackT_;
    Source source_ = Source::Live;
    std::string status_;
    std::string recordError_;
    std::unique_ptr<f1sim::Sim> sim_;
    std::chrono::steady_clock::time_point lastData_, rateT_;
    uint64_t lastReceived_ = 0, lastPerId_[16] = {};
    std::vector<uint64_t> lastSent_;
    double rate_ = 0, perIdRate_[16] = {};
    // packet monitor for the log: summary every 10 s, high rates and silences at once
    void monitor(const f1hub::Stats &st, std::chrono::steady_clock::time_point now, double dt);
    std::chrono::steady_clock::time_point monT_, highLogT_;
    f1hub::Stats monLast_;
    bool monHadData_ = false, monSilent_ = false;
    uint16_t monFormat_ = 0;
    std::vector<double> targetRate_;
};

std::string timestampName();
std::string formatDuration(double seconds); // "1:02:03" / "12:34"
std::string formatBytes(uint64_t b);

} // namespace app
