// Circuit outlines for the track map: centre line + track widths for every F1 25 circuit (bundled data, see
// scripts/gen_tracks.py), and the fit that places one into the game's world coordinates.
//
// The game sends no track geometry, only car positions. The bundled outlines are in their own local frame, so they
// are aligned to the player's driven line: each driven point (lap distance d, world x/z) is paired with the outline
// point at the same distance along the lap, trying every start offset, both directions and both handedness, and the
// best 2D similarity transform (rotation, scale, translation) wins.
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace tracks {

struct Shape {
    int gameId = -1;
    bool reversed = false; // the game's reverse layout of a circuit
    std::string name, source;
    std::vector<float> x, y, wr, wl; // closed centre line, one point per ~5 m
    float length = 0;
};

// The bundled outline for a game track id (Session packet), or nullptr when there is none.
const Shape *forGame(int trackId);
std::vector<int> allGameIds();
// ASCII name for file names, e.g. "Monza"; "Track12" style for unknown ids.
std::string asciiName(int trackId);

struct DrivenPoint {
    float dist, x, z; // lap distance (m), world position
};

// The outline in game world coordinates.
struct Geometry {
    bool ok = false;
    float rms = 0;   // fit error, m
    float scale = 1; // outline metres -> game metres
    std::string name, source;
    int season = 0; // calibrated maps: the game's season (2025 for F1 25), 0 = unknown
    std::vector<float> cx, cz, lx, lz, rx, rz; // centre and both edges
    std::vector<float> dist;                   // game lap distance of each point
    // calibrated maps: the fitted kerbs at each point, as distances inside the left / right edge (m): kl / kr its
    // inner side, klo / kro its outer side; kl = 0 where there is no kerb
    std::vector<float> kl, kr, klo, kro;
    std::vector<std::array<float, 2>> switches; // tarmac <-> kerb switch points the kerbs were fitted to (x, z)
    // the fitted map as regions (calibrated maps): per segment from point i to i+1 and side, a band between two
    // offsets from the centre line (in0 / out0 at point i, in1 / out1 at i + 1, measured towards that side's edge)
    // and its class (SurfaceClass). Bounded by the fitted curves, so it is drawn as exact as they are.
    struct Band {
        uint32_t i;
        uint8_t side;
        float in0, in1, out0, out1;
        uint8_t cls;
    };
    std::vector<Band> bands;
    // DRS zones ('D'), as lap distance ranges along the centre line (attachZones)
    struct Zone {
        float d0, d1;
        char kind;
    };
    std::vector<Zone> zones;
    // what the ground is, scanned by the cars' wheels (calibrated maps only): cells of `cell` metres, the surface
    // the wheels found there most often - tarmac too, so the map can show what has been driven over
    struct SurfaceCell {
        int32_t x, z; // cell index: world x / cell, world z / cell
        uint8_t cls;  // SurfaceClass
    };
    float cell = 0.5f;
    std::vector<SurfaceCell> surface;
    // the same cells merged along x into runs of one class, for drawing (buildSurfaceRuns)
    struct SurfaceRun {
        int32_t z, x0, x1; // cells x0..x1 inclusive in row z
        uint8_t cls;
    };
    std::vector<SurfaceRun> runs;
};

// Merges the surface cells into runs (call after filling `surface`).
void buildSurfaceRuns(Geometry &g);

// Ground classes of the surface scan.
enum SurfaceClass : uint8_t { S_TARMAC, S_KERB, S_GRASS, S_GRAVEL, S_HARD, S_WATER, S_CLASSES };
// map cells only, inferred by the calibration where no wheel went: tarmac between the edges, kerb gaps, the ground
// beyond the edges
enum InferredClass : uint8_t { S_INFERRED = S_CLASSES, S_INF_KERB, S_INF_GRASS, S_INF_GRAVEL, S_INF_WATER, S_ALL };
// The class of a CarTelemetry surface type: grass; gravel, sand, mud, rock; concrete, cobbles, metal, ridged ...
SurfaceClass surfaceClassOf(uint8_t surfaceType);

// Track N Race maps (third_party/track-n-race, GPL-3.0, by NoGoat; scripts/gen_tnr_tracks.py): per F1 25 circuit
// the cars' median line in game world coordinates, from the start line round, with its sector boundaries, DRS zones
// and speed trap (F1 25 only: the 2026 season pack's tracks and zones are left out).
struct TnrTrack {
    int gameId = -1;
    std::string name, circuit;
    float length = 0;            // m
    std::vector<float> x, z;     // the line, world coordinates, starting at the start line
    int sector2 = 0, sector3 = 0; // indices where sectors 2 and 3 begin
    struct Zone {
        float x0, z0, x1, z1; // start, end
        char kind;            // 'D' DRS
    };
    std::vector<Zone> zones;
    float startX = 0, startZ = 0;
    bool hasTrap = false;
    float trapX = 0, trapZ = 0;
};
const TnrTrack *tnrForGame(int trackId);
// A map from a Track N Race line: already in game coordinates, nothing to fit. The line is where the cars drive,
// not the middle of the track; the width comes from the bundled outline (its median) or a typical 12 m.
Geometry fromTnr(const TnrTrack &t, float gameLength);
// The track's DRS zones (from its Track N Race map) as lap distance ranges along `g`.
void attachZones(Geometry &g, int trackId);

// Fits `shape` to the driven points. `gameLength` is the lap length from the Session packet. Needs points covering at
// least ~1.2 km of the lap; `maxRms` is the largest fit error (m) still accepted.
Geometry align(const Shape &shape, const std::vector<DrivenPoint> &driven, float gameLength, float maxRms = 25.f);

} // namespace tracks
