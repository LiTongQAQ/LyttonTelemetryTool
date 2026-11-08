#include "compare.hpp"

#include "race_state.hpp"
#include "replay.hpp"
#include "tracks.hpp"

#include <algorithm>
#include <filesystem>
#include <thread>

namespace cmp {

std::shared_ptr<File> load(const std::string &path)
{
    auto f = std::make_shared<File>();
    f->path = path;
    f->name = std::filesystem::path(path).filename().string();
    timing::RaceState race;
    timing::TraceLog trace;
    {
        f1hub::Replayer rp([&](const uint8_t *d, size_t n) { race.feed(d, n); trace.feed(d, n); }, [&] {
            race.reset();
            trace.reset();
        });
        std::string err;
        if (!rp.open(path, &err)) {
            f->error = err;
            return f;
        }
        rp.seek(rp.duration()); // feeds every packet on this thread
    }
    const auto snap = race.snapshot();
    if (snap.playerIdx < f1tel::MAX_CARS) f->driver = snap.cars[snap.playerIdx].name;
    f->trackId = snap.session.trackId;
    f->trackLength = snap.session.trackLength;
    f->s2 = snap.session.sector2Start;
    f->s3 = snap.session.sector3Start;
    const timing::Car *me = snap.playerIdx < f1tel::MAX_CARS ? &snap.cars[snap.playerIdx] : nullptr;
    trace.read([&](const timing::TraceData &d) {
        if (f->trackLength <= 0) f->trackLength = d.trackLength;
        if (f->s2 <= 0) f->s2 = d.sectorDist[0];
        if (f->s3 <= 0) f->s3 = d.sectorDist[1];
        for (const auto &l : d.laps) {
            if (!l.complete || l.s.size() < 50 || !l.lapMs) continue;
            Lap lap;
            lap.lapNum = l.lapNum;
            lap.lapMs = l.lapMs;
            lap.valid = l.valid;
            lap.trace = l;
            if (me && l.lapNum >= 1 && size_t(l.lapNum) <= me->laps.size()) {
                const auto &h = me->laps[l.lapNum - 1];
                lap.sectorMs = h.sectorMs;
                if (h.lapMs) lap.valid = h.lapValid;
            }
            f->laps.push_back(std::move(lap));
        }
    });
    for (int i = 0; i < int(f->laps.size()); i++)
        if (f->laps[i].valid && (f->bestIdx < 0 || f->laps[i].lapMs < f->laps[f->bestIdx].lapMs)) f->bestIdx = i;
    if (f->laps.empty() && f->error.empty()) f->error = "No complete lap (needs line to line)";
    return f;
}

std::shared_ptr<const File> Cache::get(const std::string &path)
{
    std::lock_guard<std::mutex> l(m_);
    auto &e = files_[path];
    if (!e) {
        e = std::make_shared<Entry>();
        e->busy = true;
        auto entry = e;
        threads_.push_back(new std::thread([entry, path, this] {
            auto f = load(path);
            std::lock_guard<std::mutex> g(m_);
            entry->file = f;
            entry->busy = false;
        }));
    }
    return e->busy ? nullptr : e->file;
}

bool Cache::loading(const std::string &path) const
{
    std::lock_guard<std::mutex> l(m_);
    auto it = files_.find(path);
    return it != files_.end() && it->second->busy;
}

Cache::~Cache()
{
    for (auto *t : threads_) {
        if (t->joinable()) t->join();
        delete t;
    }
}

timing::Sample sampleAtTime(const timing::LapTrace &l, float t)
{
    if (l.s.empty()) return {};
    const float t0 = l.s.front().t;
    const float abs = t0 + t;
    auto it = std::lower_bound(l.s.begin(), l.s.end(), abs, [](const timing::Sample &s, float v) { return s.t < v; });
    if (it == l.s.begin()) return l.s.front();
    if (it == l.s.end()) return l.s.back();
    const auto &b = *it, &a = *(it - 1);
    const float k = b.t > a.t ? (abs - a.t) / (b.t - a.t) : 0;
    auto mix = [k](float x, float y) { return x + (y - x) * k; };
    timing::Sample r = a;
    r.t = abs;
    r.dist = mix(a.dist, b.dist);
    r.speed = mix(a.speed, b.speed);
    r.throttle = mix(a.throttle, b.throttle);
    r.brake = mix(a.brake, b.brake);
    r.steer = mix(a.steer, b.steer);
    r.rpm = mix(a.rpm, b.rpm);
    r.x = mix(a.x, b.x);
    r.z = mix(a.z, b.z);
    r.gear = k < 0.5f ? a.gear : b.gear;
    return r;
}

float distAtTime(const timing::LapTrace &l, float t)
{
    if (l.s.size() < 2 || t < 0 || l.s.front().t + t > l.s.back().t) return -1;
    return sampleAtTime(l, t).dist;
}

} // namespace cmp
