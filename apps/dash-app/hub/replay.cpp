#include "replay.hpp"
#include "hub.hpp" // openFile

#include <f1tel/parse.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>

namespace f1hub {

namespace {
const char MAGIC[4] = {'F', '1', 'T', 'R'};
}

RecordingInfo scanRecording(const std::string &path)
{
    RecordingInfo r;
    std::FILE *f = openFile(path, L"rb");
    if (!f) return r;
    char magic[4];
    uint16_t v = 0, res = 0;
    if (std::fread(magic, 1, 4, f) == 4 && std::memcmp(magic, MAGIC, 4) == 0 && std::fread(&v, 2, 1, f) == 1 &&
        std::fread(&res, 2, 1, f) == 1 && v == 1) {
        r.ok = true;
        uint64_t t;
        uint16_t n;
        std::vector<uint8_t> buf;
        while (std::fread(&t, 8, 1, f) == 1 && std::fread(&n, 2, 1, f) == 1) {
            if (r.sessionType < 0 && n == sizeof(f1tel::PacketSession)) { // read the payload of the first Session packet
                buf.resize(n);
                if (std::fread(buf.data(), 1, n, f) != n) break;
                if (auto p = f1tel::parse<f1tel::PacketSession>(buf.data(), n)) {
                    r.trackId = p->trackId;
                    r.sessionType = p->sessionType;
                }
            } else if (std::fseek(f, n, SEEK_CUR) != 0) {
                break;
            }
            r.durationNs = t;
            r.packets++;
        }
    }
    std::fclose(f);
    std::error_code ec;
    r.bytes = std::filesystem::file_size(path, ec);
    return r;
}

Replayer::Replayer(Sink sink, std::function<void()> reset) : sink_(std::move(sink)), reset_(std::move(reset))
{
    thread_ = std::thread([this] { loop(); });
}

Replayer::~Replayer()
{
    {
        std::lock_guard<std::mutex> l(m_);
        quit_ = true;
    }
    cv_.notify_all();
    thread_.join();
}

bool Replayer::open(const std::string &path, std::string *error)
{
    std::FILE *f = openFile(path, L"rb");
    if (!f) {
        if (error) *error = "Cannot open the file";
        return false;
    }
    std::fseek(f, 0, SEEK_END);
    long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    std::vector<uint8_t> buf(size > 0 ? size_t(size) : 0);
    const bool readOk = std::fread(buf.data(), 1, buf.size(), f) == buf.size();
    std::fclose(f);
    if (!readOk || buf.size() < 8 || std::memcmp(buf.data(), MAGIC, 4) != 0 || buf[4] != 1 || buf[5] != 0) {
        if (error) *error = "Not a .f1tr recording (or an unsupported version)";
        return false;
    }

    std::vector<Rec> index;
    std::vector<LapMark> laps;
    std::vector<PhaseSpan> phases;
    int lastLap = -1;
    // open spans: the car's state (-1 = nothing to mark) and the safety car's
    int carKind = -1, scKind = -1;
    uint64_t carT = 0, scT = 0, lastT = 0;
    auto track = [&](int &open, uint64_t &since, int kind, uint64_t t) {
        if (kind == open) return;
        if (open >= 0 && t > since) phases.push_back({since, t, Phase(open)});
        open = kind;
        since = t;
    };
    size_t off = 8;
    while (off + 10 <= buf.size()) {
        uint64_t t;
        uint16_t n;
        std::memcpy(&t, &buf[off], 8);
        std::memcpy(&n, &buf[off + 8], 2);
        if (off + 10 + n > buf.size()) break; // truncated tail (recording cut off)
        index.push_back({t, off + 10, n});
        if (n == sizeof(f1tel::PacketLapData)) {
            if (auto p = f1tel::parse<f1tel::PacketLapData>(&buf[off + 10], n);
                p && p->header.playerCarIndex < f1tel::MAX_CARS) {
                const auto &c = p->car[p->header.playerCarIndex];
                const int lap = c.currentLapNum;
                // a flashback back over the line: the abandoned lap goes, the earlier one runs on
                while (lap > 0 && !laps.empty() && laps.back().lapNum > lap) laps.pop_back();
                if (lap != lastLap && lap > 0 && (laps.empty() || laps.back().lapNum != lap)) laps.push_back({t, lap});
                lastLap = lap;
                // garage / pit lane first, then out and in laps, then an invalid lap
                const int kind = c.driverStatus == 0 ? int(Phase::Garage)
                                 : c.pitStatus      ? int(Phase::Pit)
                                 : c.driverStatus == 3 ? int(Phase::OutLap)
                                 : c.driverStatus == 2 ? int(Phase::InLap)
                                 : c.currentLapInvalid ? int(Phase::Invalid)
                                                       : -1;
                track(carKind, carT, kind, t);
            }
        } else if (n == sizeof(f1tel::PacketSession)) {
            if (auto p = f1tel::parse<f1tel::PacketSession>(&buf[off + 10], n)) {
                const int sc = p->safetyCarStatus;
                track(scKind, scT, sc == 1 ? int(Phase::SafetyCar) : sc == 2 ? int(Phase::VirtualSC) : -1, t);
            }
        }
        lastT = t;
        off += 10 + n;
    }

    track(carKind, carT, -1, lastT);
    track(scKind, scT, -1, lastT);
    std::sort(phases.begin(), phases.end(), [](const PhaseSpan &x, const PhaseSpan &y) { return x.t0 < y.t0; });

    std::lock_guard<std::mutex> e(emit_);
    std::unique_lock<std::mutex> l(m_);
    playing_ = false;
    path_ = path;
    buf_ = std::move(buf);
    index_ = std::move(index);
    laps_ = std::move(laps);
    phases_ = std::move(phases);
    duration_ = index_.empty() ? 0 : index_.back().t;
    next_ = 0;
    pos_ = 0;
    l.unlock();
    if (reset_) reset_();
    return true;
}

void Replayer::close()
{
    std::lock_guard<std::mutex> e(emit_);
    std::lock_guard<std::mutex> l(m_);
    playing_ = false;
    path_.clear();
    buf_.clear();
    index_.clear();
    laps_.clear();
    phases_.clear();
    duration_ = 0;
    next_ = 0;
    pos_ = 0;
}

bool Replayer::isOpen() const
{
    std::lock_guard<std::mutex> l(m_);
    return !index_.empty();
}

void Replayer::play()
{
    std::lock_guard<std::mutex> l(m_);
    if (index_.empty()) return;
    if (next_ >= index_.size()) { // at the end: start over
        playing_ = false;
        next_ = 0;
        pos_ = 0;
        if (reset_) reset_();
    }
    playing_ = true;
    baseWall_ = std::chrono::steady_clock::now();
    cv_.notify_all();
}

void Replayer::pause()
{
    std::lock_guard<std::mutex> l(m_);
    if (!playing_) return;
    pos_ += uint64_t(std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - baseWall_).count() * speed_);
    pos_ = std::min(pos_, duration_);
    playing_ = false;
}

bool Replayer::playing() const
{
    std::lock_guard<std::mutex> l(m_);
    return playing_;
}

void Replayer::setSpeed(double speed)
{
    std::lock_guard<std::mutex> l(m_);
    if (playing_) {
        auto now = std::chrono::steady_clock::now();
        pos_ += uint64_t(std::chrono::duration<double, std::nano>(now - baseWall_).count() * speed_);
        baseWall_ = now;
    }
    speed_ = std::clamp(speed, 0.05, 64.0);
}

double Replayer::speed() const
{
    std::lock_guard<std::mutex> l(m_);
    return speed_;
}

uint64_t Replayer::position() const
{
    std::lock_guard<std::mutex> l(m_);
    if (!playing_) return pos_;
    uint64_t p = pos_ + uint64_t(std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - baseWall_).count() * speed_);
    return std::min(p, duration_);
}

void Replayer::seek(uint64_t tNs)
{
    std::lock_guard<std::mutex> e(emit_);
    std::unique_lock<std::mutex> l(m_);
    if (index_.empty()) return;
    tNs = std::min(tNs, duration_);
    const uint64_t cur = next_ ? index_[next_ - 1].t : 0;
    if (tNs < cur) {
        next_ = 0;
        l.unlock();
        if (reset_) reset_();
        l.lock();
    }
    seeking_ = true;
    emitUntil(tNs);
    seeking_ = false;
    pos_ = tNs;
    baseWall_ = std::chrono::steady_clock::now();
}

void Replayer::emitUntil(uint64_t tNs)
{
    // The sink may take a while (subscribers, forwarding); hand it packets in batches with the lock released.
    std::vector<uint8_t> chunk;
    std::vector<std::pair<size_t, uint16_t>> items;
    while (next_ < index_.size() && index_[next_].t <= tNs) {
        chunk.clear();
        items.clear();
        for (int k = 0; k < 512 && next_ < index_.size() && index_[next_].t <= tNs; k++, next_++) {
            const Rec &r = index_[next_];
            items.push_back({chunk.size(), r.len});
            chunk.insert(chunk.end(), buf_.begin() + r.off, buf_.begin() + r.off + r.len);
        }
        m_.unlock();
        for (auto [o, n] : items) sink_(chunk.data() + o, n);
        m_.lock();
    }
}

void Replayer::loop()
{
    for (;;) {
        {
            std::unique_lock<std::mutex> l(m_);
            cv_.wait(l, [&] { return quit_ || playing_; });
            if (quit_) return;
        }
        {
            std::lock_guard<std::mutex> e(emit_);
            std::unique_lock<std::mutex> l(m_);
            if (quit_) return;
            if (playing_) {
                const auto now = std::chrono::steady_clock::now();
                emitUntil(pos_ + uint64_t(std::chrono::duration<double, std::nano>(now - baseWall_).count() * speed_));
                if (next_ >= index_.size()) { // reached the end
                    pos_ = duration_;
                    playing_ = false;
                }
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
}

} // namespace f1hub
