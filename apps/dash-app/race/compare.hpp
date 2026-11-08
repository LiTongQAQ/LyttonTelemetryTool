// Laps out of recordings for the comparison page: a .f1tr file is played through a private RaceState + TraceLog
// (not through the hub, so nothing is forwarded) and the player's laps are kept with their traces and sector times.
// Files load on a background thread and stay cached.
#pragma once

#include "trace.hpp"

#include <array>
#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace cmp {

struct Lap {
    int lapNum = 0;
    uint32_t lapMs = 0;
    std::array<uint32_t, 3> sectorMs{}; // from the game's lap history; 0 = unknown
    bool valid = true;
    timing::LapTrace trace;
};

struct File {
    std::string path, name, driver, error;
    int trackId = -1;
    float trackLength = 0, s2 = 0, s3 = 0; // m (Session packet)
    std::vector<Lap> laps;                // complete laps with a trace, in driving order
    int bestIdx = -1;                     // fastest valid lap
};

// Blocking: reads the whole recording.
std::shared_ptr<File> load(const std::string &path);

class Cache {
public:
    // The loaded file, or nullptr while it is loading (loading starts on the first call).
    std::shared_ptr<const File> get(const std::string &path);
    bool loading(const std::string &path) const;
    ~Cache();

private:
    struct Entry {
        std::shared_ptr<const File> file;
        std::atomic<bool> busy{false};
    };
    mutable std::mutex m_;
    std::map<std::string, std::shared_ptr<Entry>> files_;
    std::vector<std::thread *> threads_;
};

// Interpolated sample at a time into the lap (s); clamps to the ends.
timing::Sample sampleAtTime(const timing::LapTrace &l, float t);
// Lap distance reached at a time into the lap (m), -1 outside the trace.
float distAtTime(const timing::LapTrace &l, float t);

} // namespace cmp
