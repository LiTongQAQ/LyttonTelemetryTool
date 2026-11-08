// Track calibration from recordings: the bundled outline (tracks.hpp) is placed in the game's world coordinates using
// only what was recorded, and the result is saved as a map file the program draws directly.
//
//   1. extract(): reads .f1tr recordings and keeps, per track, (a) where the cars on track are, averaged per 5 m of
//      lap distance (cars in the pit lane or the garage are left out), and (b) measured points of the track edge:
//      where a wheel crossed from the track onto grass / gravel / sand (or back), from tarmac onto a kerb, or rode
//      a kerb. Any number of recordings, from any
//      driver, go into one data file per track.
//   2. calibrate(): merges one or more data files of the same track, aligns the outline to the averaged line, then
//      repeatedly refits the whole outline to the kerb points, and finally shifts it locally (per 50 m) where the
//      kerbs say the outline is off.
//   3. The map file (saveMap / loadMap) holds the outline in game coordinates; no fitting at run time.
#pragma once

#include "tracks.hpp"

#include <array>
#include <atomic>
#include <map>
#include <memory>
#include <unordered_map>
#include <string>
#include <vector>

namespace tracks {

constexpr float CAL_BIN_M = 5.f;
constexpr int CAL_POS_CAP = 150; // car positions kept per bin, for the track's width
constexpr float CAL_CELL_M = 0.5f; // surface scan cell

struct CalBin {
    double sx = 0, sz = 0; // sums of world x / z
    uint32_t n = 0;
};

// A measured point of the track edge: where a wheel was.
//   'O' the moment a wheel crossed between the track (tarmac or kerb) and grass, gravel, sand ...: the outer edge
//   'I' the moment a wheel crossed between tarmac and a kerb: the kerb's inner edge
//   'K' a wheel riding a kerb (somewhere across its width, so the least precise)
struct CalEdge {
    float dist, x, z;
    char side;       // 'L' / 'R': which side of the car the wheel was on
    char kind = 'K';
};

struct CalData {
    int trackId = -1;
    float trackLength = 0;           // m (Session packet)
    int gameYear = 0;                // from the packet header: 25 = F1 25 (the 2025 season)
    std::vector<std::string> sources; // recordings it came from
    uint64_t samples = 0;            // car positions added
    std::vector<CalBin> bins;        // one per CAL_BIN_M of lap distance
    std::vector<CalEdge> edges;      // kerb contacts, capped per bin and side
    // per bin, a random sample of the car positions (x, z), at most CAL_POS_CAP: where the cars actually drive
    std::vector<std::vector<std::array<float, 2>>> pos;
    // the surface scan: every on-track car's wheels mark the ground under them, per CAL_CELL_M cell: how many
    // times each SurfaceClass was found there. Key: cell x << 32 | cell z (both as uint32).
    std::unordered_map<uint64_t, std::array<uint16_t, S_CLASSES>> grid;
    float coverage() const;          // share of the lap with a position, 0..1
};

struct ExtractProgress {
    std::atomic<int> filesDone{0}, filesTotal{0};
    std::atomic<bool> cancel{false};
};

// Reads the recordings and returns one data set per track found in them. `log` gets a line per file.
std::vector<CalData> extract(const std::vector<std::string> &recordings, ExtractProgress *progress,
                             std::vector<std::string> *log);

// The extraction itself, one packet at a time: for recordings (extract) and for a live scan (the track recording
// page). Not thread-safe; the caller locks.
class Scanner {
public:
    Scanner();
    ~Scanner();
    void begin(const std::string &source); // a new recording / live session: its name goes into the data's sources
    void feed(const uint8_t *data, size_t len);
    int trackId() const;                   // of the session being fed, -1 before a Session packet
    std::map<int, CalData> &results();     // per track
    // edge points so far per 5 m bin: [K left, K right, I left, I right, O left, O right] (capped at 24)
    const std::vector<std::array<uint8_t, 6>> *edgeCounts(int trackId) const;
    uint64_t added = 0, kerbs = 0, dropped = 0; // since begin()
    struct Player {
        bool valid = false, onTrack = false;
        float x = 0, z = 0, fx = 0, fz = 1, dist = 0, speed = 0; // speed km/h
        uint8_t surf[4] = {};                                     // RL RR FL FR, f1tel::Surface
    } player;

private:
    struct Impl;
    std::unique_ptr<Impl> m;
};

bool saveData(const CalData &d, const std::string &path, std::string *error = nullptr);
bool loadData(const std::string &path, CalData &d, std::string *error = nullptr);
// Adds `b` to `a` (same track); false when the tracks differ.
bool mergeData(CalData &a, const CalData &b);

struct CalReport {
    bool ok = false;
    std::string message;
    float startRms = 0;       // averaged line vs. outline after the first alignment, m
    float kerbBefore = 0;     // edge points vs. nearest outline edge after the first alignment, m (rms, each kind
                              // less its own offset from the edge)
    float kerbGlobal = 0;     // ... after refitting the whole outline to the kerbs
    float kerbLocal = 0;      // ... after the local corrections
    int kerbPoints = 0, iterations = 0, segmentsCorrected = 0;
    int outerPoints = 0, innerPoints = 0, ridePoints = 0; // edge points by kind ('O', 'I', 'K')
    // how far each kind of point lies outside the outline's edge (m, negative = inside), estimated in the fit
    float offOuter = 0, offInner = 0, offRide = 0;
    // the edge points are first merged per 5 m, side and kind into consensus points (median of all cars and laps)
    int consensusPoints = 0;
    float consistency = 0; // how far the raw points lie from their consensus point (m, rms): the data's own spread
    float coverage = 0;
    // both ways of making the map, scored the same way (consensus edge points vs. the map's edges, rms), and which
    // one was used: 'O' the bundled outline fitted, 'D' the track built from the recorded data alone
    float outlineScore = 0, dataScore = 0;
    char method = 0;
    int edgesFromScan = 0, inferredCells = 0; // centre points with a measured edge; cells with an inferred surface
};

enum class CalMethod { Auto, Outline, Data };

// Makes the map from the (merged) data: the bundled outline fitted to it, or the track built from the recorded data
// alone (for a layout the bundled outline does not have), or (Auto) whichever fits the measured edges better.
Geometry calibrate(const CalData &d, CalReport *report, CalMethod method = CalMethod::Auto);

// A track built from the recorded data alone: the cars' averaged line, the edges where the cars' positions end (plus
// half a car) or where a wheel crossed onto grass / gravel. Needs the position samples.
Geometry buildFromData(const CalData &d, std::string *why = nullptr);

bool parseMap(const std::string &text, Geometry &g, int *trackId = nullptr, std::string *info = nullptr,
              CalData *data = nullptr, bool *hasData = nullptr);

// A map file holds the map and, when `data` is given, the scan it was made from (so maps from different people can
// be integrated: their data merged and calibrated again).
bool saveMap(const Geometry &g, int trackId, const CalReport &r, const std::vector<std::string> &sources,
             const std::string &path, std::string *error = nullptr, const CalData *data = nullptr);
// The 2D map: centre line, edges, kerbs and the fitted regions; no scan data, cells, switch points or sources (the
// full map stays with whoever made it). Enough to show the track; it cannot be integrated or calibrated again.
bool saveDisplayMap(const Geometry &g, int trackId, const std::string &path, std::string *error = nullptr);
bool loadMap(const std::string &path, Geometry &g, int *trackId = nullptr, std::string *info = nullptr,
             CalData *data = nullptr, bool *hasData = nullptr);

} // namespace tracks
