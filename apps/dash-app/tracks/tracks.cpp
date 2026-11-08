#include "tracks.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <memory>

namespace tracks {

namespace {

struct TrackEntry {
    int gameId;
    bool reversed;
    const char *name, *source;
    const float *pts; // x, y, wr, wl per point
    int count;
    float length;
};

#include "track_data.inc"

struct TnrZoneData {
    float x0, z0, x1, z1;
    char kind;
};
struct TnrTrackData {
    int gameId;
    const char *name, *circuit;
    int length;
    const float *pts;
    int count, sector2, sector3;
    const TnrZoneData *zones;
    int zoneCount;
    float startX, startZ;
    int hasTrap;
    float trapX, trapZ;
};
#include "tnr_data.inc"

// Closed curve sampled every metre, for O(1) lookups by distance along it.
struct Uniform {
    std::vector<float> x, y;
    float len = 0;
    void at(float s, float &ox, float &oy) const
    {
        const int n = int(x.size());
        s = std::fmod(s, len);
        if (s < 0) s += len;
        const float f = s / len * float(n);
        int i = int(f);
        const float k = f - float(i);
        i %= n;
        const int j = (i + 1) % n;
        ox = x[i] + (x[j] - x[i]) * k;
        oy = y[i] + (y[j] - y[i]) * k;
    }
};

Uniform uniform(const Shape &sh)
{
    Uniform u;
    const int n = int(sh.x.size());
    std::vector<float> cum(n + 1, 0.f);
    for (int i = 0; i < n; i++) {
        const int j = (i + 1) % n;
        cum[i + 1] = cum[i] + std::hypot(sh.x[j] - sh.x[i], sh.y[j] - sh.y[i]);
    }
    u.len = cum[n];
    const int m = std::max(8, int(u.len));
    u.x.resize(m);
    u.y.resize(m);
    int seg = 0;
    for (int k = 0; k < m; k++) {
        const float s = u.len * float(k) / float(m);
        while (cum[seg + 1] < s) seg++;
        const int j = (seg + 1) % n;
        const float t = (s - cum[seg]) / std::max(1e-6f, cum[seg + 1] - cum[seg]);
        u.x[k] = sh.x[seg] + (sh.x[j] - sh.x[seg]) * t;
        u.y[k] = sh.y[seg] + (sh.y[j] - sh.y[seg]) * t;
    }
    return u;
}

struct Fit {
    double resid = 1e30;
    int mirror = 1, dir = 1;
    float offset = 0;
    double cosS = 1, sinS = 0; // scale * cos / sin
    double ax = 0, ay = 0, qx = 0, qy = 0; // centroids
};

// Best similarity transform taking outline points at (offset + dir * d * ratio) onto the driven points.
Fit fitAt(const Uniform &u, const std::vector<DrivenPoint> &pts, float ratio, int mirror, int dir, float offset)
{
    const size_t n = pts.size();
    std::vector<float> axs(n), ays(n);
    double ax = 0, ay = 0, qx = 0, qy = 0;
    for (size_t k = 0; k < n; k++) {
        u.at(offset + float(dir) * pts[k].dist * ratio, axs[k], ays[k]);
        ax += axs[k];
        ay += ays[k];
        qx += pts[k].x;
        qy += mirror * pts[k].z;
    }
    ax /= double(n); ay /= double(n); qx /= double(n); qy /= double(n);
    double a = 0, b = 0, n2 = 0, q2 = 0;
    for (size_t k = 0; k < n; k++) {
        const double px = axs[k] - ax, py = ays[k] - ay;
        const double tx = pts[k].x - qx, ty = mirror * pts[k].z - qy;
        a += px * tx + py * ty;
        b += px * ty - py * tx;
        n2 += px * px + py * py;
        q2 += tx * tx + ty * ty;
    }
    Fit f;
    if (n2 < 1e-6) return f;
    f.resid = q2 - (a * a + b * b) / n2;
    f.mirror = mirror;
    f.dir = dir;
    f.offset = offset;
    f.cosS = a / n2;
    f.sinS = b / n2;
    f.ax = ax; f.ay = ay; f.qx = qx; f.qy = qy;
    return f;
}

} // namespace

const TnrTrack *tnrForGame(int trackId)
{
    static std::map<int, std::unique_ptr<TnrTrack>> cache;
    auto it = cache.find(trackId);
    if (it != cache.end()) return it->second.get();
    for (const auto &e : TNR_TRACKS) {
        if (e.gameId != trackId) continue;
        auto t = std::make_unique<TnrTrack>();
        t->gameId = e.gameId;
        t->name = e.name;
        t->circuit = e.circuit;
        t->length = float(e.length);
        for (int i = 0; i < e.count; i++) {
            t->x.push_back(e.pts[i * 2]);
            t->z.push_back(e.pts[i * 2 + 1]);
        }
        t->sector2 = e.sector2;
        t->sector3 = e.sector3;
        for (int i = 0; i < e.zoneCount; i++)
            t->zones.push_back({e.zones[i].x0, e.zones[i].z0, e.zones[i].x1, e.zones[i].z1, e.zones[i].kind});
        t->startX = e.startX;
        t->startZ = e.startZ;
        t->hasTrap = e.hasTrap != 0;
        t->trapX = e.trapX;
        t->trapZ = e.trapZ;
        return (cache[trackId] = std::move(t)).get();
    }
    cache[trackId] = nullptr;
    return nullptr;
}

Geometry fromTnr(const TnrTrack &t, float gameLength)
{
    Geometry g;
    const size_t n = t.x.size();
    if (n < 20) return g;
    // the line's own length, then points every 5 m along it, lap distance scaled to the game's lap length
    std::vector<double> cum(n + 1, 0);
    for (size_t i = 0; i < n; i++) {
        const size_t j = (i + 1) % n;
        cum[i + 1] = cum[i] + std::hypot(double(t.x[j] - t.x[i]), double(t.z[j] - t.z[i]));
    }
    const double total = cum[n];
    if (total < 500) return g;
    const double scale = (gameLength > 0 ? gameLength : t.length) / total;
    float half = 6.f;
    if (const Shape *sh = forGame(t.gameId); sh && !sh->wl.empty()) {
        std::vector<float> w;
        for (size_t i = 0; i < sh->wl.size(); i++) w.push_back((sh->wl[i] + sh->wr[i]) / 2);
        std::nth_element(w.begin(), w.begin() + w.size() / 2, w.end());
        half = std::clamp(w[w.size() / 2], 4.f, 10.f);
    }
    const int steps = int(total / 5.0);
    size_t seg = 0;
    std::vector<float> cx, cz;
    for (int s = 0; s < steps; s++) {
        const double d = total * s / steps;
        while (seg + 1 < n && cum[seg + 1] < d) seg++;
        const size_t j = (seg + 1) % n;
        const double f = (d - cum[seg]) / std::max(1e-9, cum[seg + 1] - cum[seg]);
        cx.push_back(float(t.x[seg] + (t.x[j] - t.x[seg]) * f));
        cz.push_back(float(t.z[seg] + (t.z[j] - t.z[seg]) * f));
        g.dist.push_back(float(d * scale));
    }
    const size_t m = cx.size();
    for (size_t i = 0; i < m; i++) {
        const size_t a = (i + m - 1) % m, b = (i + 1) % m;
        float tx = cx[b] - cx[a], tz = cz[b] - cz[a];
        const float l = std::max(1e-4f, std::hypot(tx, tz));
        const float nx = -tz / l, nz = tx / l;
        g.cx.push_back(cx[i]);
        g.cz.push_back(cz[i]);
        g.lx.push_back(cx[i] + nx * half);
        g.lz.push_back(cz[i] + nz * half);
        g.rx.push_back(cx[i] - nx * half);
        g.rz.push_back(cz[i] - nz * half);
    }
    g.name = asciiName(t.gameId); // English names in the interface
    g.source = "Track N Race";
    g.season = 2025;
    g.ok = true;
    attachZones(g, t.gameId);
    return g;
}

void attachZones(Geometry &g, int trackId)
{
    g.zones.clear();
    const TnrTrack *t = tnrForGame(trackId);
    if (!t || g.cx.empty() || g.dist.size() != g.cx.size()) return;
    auto distAt = [&](float x, float z) {
        size_t best = 0;
        float bd = 1e30f;
        for (size_t i = 0; i < g.cx.size(); i++) {
            const float d = (g.cx[i] - x) * (g.cx[i] - x) + (g.cz[i] - z) * (g.cz[i] - z);
            if (d < bd) { bd = d; best = i; }
        }
        return bd < 30.f * 30.f ? g.dist[best] : -1.f; // more than 30 m off: not this layout
    };
    for (const auto &z : t->zones) {
        const float d0 = distAt(z.x0, z.z0), d1 = distAt(z.x1, z.z1);
        if (d0 >= 0 && d1 >= 0) g.zones.push_back({d0, d1, z.kind});
    }
}

const Shape *forGame(int trackId)
{
    static std::map<int, std::unique_ptr<Shape>> cache;
    auto it = cache.find(trackId);
    if (it != cache.end()) return it->second.get();
    for (const auto &e : TRACK_TABLE) {
        if (e.gameId != trackId) continue;
        auto s = std::make_unique<Shape>();
        s->gameId = e.gameId;
        s->reversed = e.reversed;
        s->name = e.name;
        s->source = e.source;
        s->length = e.length;
        for (int i = 0; i < e.count; i++) {
            s->x.push_back(e.pts[i * 4]);
            s->y.push_back(e.pts[i * 4 + 1]);
            s->wr.push_back(e.pts[i * 4 + 2]);
            s->wl.push_back(e.pts[i * 4 + 3]);
        }
        return (cache[trackId] = std::move(s)).get();
    }
    cache[trackId] = nullptr;
    return nullptr;
}

std::string asciiName(int id)
{
    static const std::map<int, const char *> n = {
        {0, "Melbourne"}, {2, "Shanghai"}, {3, "Sakhir"}, {4, "Catalunya"}, {5, "Monaco"}, {6, "Montreal"},
        {7, "Silverstone"}, {9, "Hungaroring"}, {10, "Spa"}, {11, "Monza"}, {12, "Singapore"}, {13, "Suzuka"},
        {14, "AbuDhabi"}, {15, "Austin"}, {16, "Interlagos"}, {17, "Austria"}, {19, "Mexico"}, {20, "Baku"},
        {26, "Zandvoort"}, {27, "Imola"}, {29, "Jeddah"}, {30, "Miami"}, {31, "LasVegas"}, {32, "Losail"},
        {39, "SilverstoneRev"}, {40, "AustriaRev"}, {41, "ZandvoortRev"}};
    auto it = n.find(id);
    return it != n.end() ? it->second : "Track" + std::to_string(id);
}

void buildSurfaceRuns(Geometry &g)
{
    auto cells = g.surface;
    std::sort(cells.begin(), cells.end(), [](const Geometry::SurfaceCell &a, const Geometry::SurfaceCell &b) {
        return a.z != b.z ? a.z < b.z : a.x < b.x;
    });
    g.runs.clear();
    for (const auto &c : cells) {
        if (!g.runs.empty()) {
            auto &r = g.runs.back();
            if (r.z == c.z && r.cls == c.cls && r.x1 + 1 == c.x) { r.x1 = c.x; continue; }
        }
        g.runs.push_back({c.z, c.x, c.x, c.cls});
    }
}

SurfaceClass surfaceClassOf(uint8_t s)
{
    switch (s) { // f1tel::Surface
    case 0: return S_TARMAC;
    case 1: return S_KERB;
    case 7: return S_GRASS;
    case 3: case 4: case 5: case 6: return S_GRAVEL; // rock, gravel, mud, sand
    case 8: return S_WATER;
    default: return S_HARD; // concrete, cobblestone, metal, ridged
    }
}

std::vector<int> allGameIds()
{
    std::vector<int> v;
    for (const auto &e : TRACK_TABLE) v.push_back(e.gameId);
    return v;
}

Geometry align(const Shape &shape, const std::vector<DrivenPoint> &drivenIn, float gameLength, float maxRms)
{
    Geometry g;
    g.name = asciiName(shape.gameId);
    g.source = shape.source;
    if (shape.x.size() < 8 || gameLength < 500) return g;

    // driven points inside the lap, thinned to a few hundred
    std::vector<DrivenPoint> all;
    for (const auto &p : drivenIn)
        if (p.dist >= 0 && p.dist <= gameLength) all.push_back(p);
    std::sort(all.begin(), all.end(), [](const DrivenPoint &a, const DrivenPoint &b) { return a.dist < b.dist; });
    if (all.size() < 20) return g;
    if (all.back().dist - all.front().dist < std::min(1200.f, gameLength * 0.25f)) return g;
    std::vector<DrivenPoint> pts;
    const size_t want = 240;
    for (size_t k = 0; k < want; k++) pts.push_back(all[k * (all.size() - 1) / (want - 1)]);

    const Uniform u = uniform(shape);
    const float ratio = u.len / gameLength;
    Fit best;
    for (int mirror : {1, -1})
        for (int dir : {1, -1})
            for (float o = 0; o < u.len; o += 3.f) {
                Fit f = fitAt(u, pts, ratio, mirror, dir, o);
                if (f.resid < best.resid) best = f;
            }
    for (float o = best.offset - 3.f; o <= best.offset + 3.f; o += 0.25f) {
        Fit f = fitAt(u, pts, ratio, best.mirror, best.dir, o);
        if (f.resid < best.resid) best = f;
    }
    g.rms = float(std::sqrt(std::max(0.0, best.resid) / double(pts.size())));
    g.scale = float(std::hypot(best.cosS, best.sinS));
    g.ok = g.rms <= maxRms && g.scale > 0.85f && g.scale < 1.15f;
    if (!g.ok) return g;

    auto toGame = [&](float ox, float oy, float &gx, float &gz) {
        const double px = ox - best.ax, py = oy - best.ay;
        const double tx = best.cosS * px - best.sinS * py + best.qx;
        const double ty = best.sinS * px + best.cosS * py + best.qy;
        gx = float(tx);
        gz = float(best.mirror * ty);
    };
    const int n = int(shape.x.size());
    const float spacing = u.len / float(n);
    for (float s = 0; s < u.len; s += 2.f) {
        float cx, cy, ax, ay, bx, by;
        u.at(s, cx, cy);
        u.at(s - 1.f, ax, ay);
        u.at(s + 1.f, bx, by);
        float tx = bx - ax, ty = by - ay;
        const float tl = std::max(1e-4f, std::hypot(tx, ty));
        tx /= tl;
        ty /= tl;
        const float nx = -ty, ny = tx; // left of the outline's own direction
        const float fi = s / spacing;
        const int i = int(fi) % n, j = (i + 1) % n;
        const float k = fi - std::floor(fi);
        const float wl = shape.wl[i] + (shape.wl[j] - shape.wl[i]) * k;
        const float wr = shape.wr[i] + (shape.wr[j] - shape.wr[i]) * k;
        float gx, gz;
        toGame(cx, cy, gx, gz);
        g.cx.push_back(gx);
        g.cz.push_back(gz);
        toGame(cx + nx * wl, cy + ny * wl, gx, gz);
        g.lx.push_back(gx);
        g.lz.push_back(gz);
        toGame(cx - nx * wr, cy - ny * wr, gx, gz);
        g.rx.push_back(gx);
        g.rz.push_back(gz);
        float d = (s - best.offset) * float(best.dir) / ratio;
        d = std::fmod(d, gameLength);
        if (d < 0) d += gameLength;
        g.dist.push_back(d);
    }
    return g;
}

} // namespace tracks
