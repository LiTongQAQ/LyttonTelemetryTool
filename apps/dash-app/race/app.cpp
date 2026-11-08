#include "app.hpp"

#include "log.hpp"


#include <algorithm>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

namespace fs = std::filesystem;

namespace app {

using layout::Offset;
using layout::PANELS;
static const char *const PANEL_KEYS[PANELS] = {"tower", "sectors", "pedals", "car", "status", "weather"};

// ---- settings ------------------------------------------------------------------------------------------------

void saveSettings(const Settings &s, const std::string &path)
{
    std::ofstream f{fs::path(path)}; // a folder with a non-ASCII name too
    f << "# Lytton Telemetry Tool settings (saved by the program)\n"
         "# forward = host:port|on(1/0)|note\n"
         "# <panel>.place = left-right(deg) up-down(deg) fore-aft(m, negative = farther) yaw(deg) pitch(deg) scale(x)\n"
         "# hmd.* = headset values by hand, 0 = what SteamVR reports; hmd.viewH/viewV = usable view the panels hug (deg)\n"
      << "port=" << s.port << "\nstartSource=" << s.startSource << "\nsimSpeed=" << s.simSpeed
      << "\nmyCode=" << s.myCode << "\nlatinFont=" << s.latinFont << "\nlatinDigits=" << s.latinDigits << "\ngapMode=" << s.gapMode << "\nbgOpacity=" << s.bgOpacity
      << "\ntextOpacity=" << s.textOpacity << "\nhmd.renderW=" << s.hmd.renderW << "\nhmd.renderH=" << s.hmd.renderH
      << "\nhmd.fovH=" << s.hmd.fovH << "\nhmd.fovV=" << s.hmd.fovV << "\nhmd.viewH=" << s.hmd.viewH
      << "\nhmd.viewV=" << s.hmd.viewV << "\n";
    for (const auto &t : s.targets) f << "forward=" << t.host << ":" << t.port << "|" << t.enabled << "|" << t.note << "\n";
    for (int i = 0; i < PANELS; i++) {
        const auto &o = s.offset[i];
        f << PANEL_KEYS[i] << ".place=" << o.daz << " " << o.del << " " << o.dz << " " << o.dyaw << " " << o.dpitch << " "
          << o.scale << "\n"
          << PANEL_KEYS[i] << ".cockpit=" << o.cockpit << " " << o.cx << " " << o.cy << " " << o.cz << "\n"
          << PANEL_KEYS[i] << ".visible=" << o.visible << "\n";
    }
}

static f1hub::Target parseTarget(const std::string &item)
{
    f1hub::Target t;
    auto colon = item.rfind(':');
    if (colon != std::string::npos) {
        t.host = item.substr(0, colon);
        t.port = uint16_t(std::atoi(item.c_str() + colon + 1));
    } else {
        t.port = uint16_t(std::atoi(item.c_str()));
    }
    return t;
}

Settings loadSettings(const std::string &path)
{
    Settings s;
    for (int i = 0; i < PANELS; i++) s.offset[i] = layout::defaultOffset(i);
    std::ifstream f{fs::path(path)};
    if (!f) return s;
    std::string line;
    bool sawForward = false;
    while (std::getline(f, line)) {
        if (line.rfind("\xEF\xBB\xBF", 0) == 0) line.erase(0, 3); // UTF-8 BOM (Notepad, PowerShell)
        if (!line.empty() && line.back() == '\r') line.pop_back();
        auto eq = line.find('=');
        if (line.empty() || line[0] == '#' || eq == std::string::npos) continue;
        std::string k = line.substr(0, eq), v = line.substr(eq + 1);
        auto num = [&] { return float(std::atof(v.c_str())); };
        if (k == "port") s.port = uint16_t(std::atoi(v.c_str()));
        else if (k == "forward" || k == "forwards") {
            if (!sawForward) s.targets.clear();
            sawForward = true;
            if (k == "forwards") { // V0.1.0: space-separated [host:]port list
                std::istringstream is(v);
                std::string item;
                while (is >> item)
                    if (auto t = parseTarget(item); t.port) s.targets.push_back(t);
            } else {
                std::string parts[3];
                std::istringstream is(v);
                for (auto &p : parts) std::getline(is, p, '|');
                auto t = parseTarget(parts[0]);
                t.enabled = parts[1] != "0";
                t.note = parts[2];
                if (t.port) s.targets.push_back(t);
            }
        }
        else if (k == "startSource") s.startSource = std::clamp(std::atoi(v.c_str()), 0, 1);
        else if (k == "simSpeed") s.simSpeed = std::clamp(num(), 0.25f, 8.f);
        else if (k == "myCode") s.myCode = v.substr(0, 4);
        else if (k == "latinFont") s.latinFont = v;
        else if (k == "latinDigits") s.latinDigits = v != "0";
        else if (k == "gapMode") s.gapMode = std::clamp(std::atoi(v.c_str()), 0, 2);
        else if (k == "autoRecord") {} // V0.1.0 setting: recording is started and stopped by hand now
        else if (k == "bgOpacity") s.bgOpacity = std::clamp(num(), 0.f, 1.f);
        else if (k == "textOpacity") s.textOpacity = std::clamp(num(), 0.1f, 1.f);
        else if (k == "hmd.renderW") s.hmd.renderW = uint32_t(std::atoi(v.c_str()));
        else if (k == "hmd.renderH") s.hmd.renderH = uint32_t(std::atoi(v.c_str()));
        else if (k == "hmd.fovH") s.hmd.fovH = num();
        else if (k == "hmd.fovV") s.hmd.fovV = num();
        else if (k == "hmd.viewH") s.hmd.viewH = num();
        else if (k == "hmd.viewV") s.hmd.viewV = num();
        else {
            for (int i = 0; i < PANELS; i++) {
                auto &o = s.offset[i];
                const std::string key = PANEL_KEYS[i];
                if (k == key + ".place") { // V0.2.0's ".offset" moved in metres: not read
                    std::istringstream is(v);
                    Offset n;
                    if (is >> n.daz >> n.del >> n.dz >> n.dyaw >> n.dpitch >> n.scale && n.scale > 0.05f) {
                        n.visible = o.visible;
                        n.cockpit = o.cockpit;
                        n.cx = o.cx; n.cy = o.cy; n.cz = o.cz;
                        o = n;
                    }
                } else if (k == key + ".cockpit") {
                    std::istringstream is(v);
                    int on = 0;
                    float x, y, z;
                    if (is >> on >> x >> y >> z) { o.cockpit = on != 0; o.cx = x; o.cy = y; o.cz = z; }
                } else if (k == key + ".visible") {
                    o.visible = v == "1";
                }
            }
        }
    }
    return s;
}

// ---- App -----------------------------------------------------------------------------------------------------

App::App(std::string d)
    : dir(std::move(d)), iniPath(dir + "/lytton.ini"),
      // a replay is forwarded only while it plays at up to 2x: not the burst after a seek, not at 16x
      replay([this](const uint8_t *p, size_t n) { hub.inject(p, n, !replay.seeking() && replay.speed() <= 2.0); },
             [this] {
                 race.reset();
                 trace.reset();
             })
{
    cfg = loadSettings(iniPath);
    timing::setMyCode(cfg.myCode);
    hub.subscribe([this](const uint8_t *p, size_t n) {
        race.feed(p, n);
        trace.feed(p, n);
    });
    applyTargets();
    lastData_ = rateT_ = std::chrono::steady_clock::now() - std::chrono::hours(1);
    rateT_ = std::chrono::steady_clock::now();
}

App::~App()
{
    stopAll();
    if (dirty) saveSettings(cfg, iniPath);
}

void App::save()
{
    saveSettings(cfg, iniPath);
    dirty = false;
    savedAt = std::time(nullptr);
}

void App::stopAll()
{
    stopRecording();
    trackGeo = {};
    trackStatus.clear();
    trackId_ = -2;
    if (sim_) sim_->stop();
    sim_.reset();
    replay.close();
    hub.stop();
    race.reset();
    trace.reset();
}

void App::useLive()
{
    stopAll();
    source_ = Source::Live;
    std::string err;
    if (hub.listen(cfg.port, &err)) status_ = "Listening on UDP " + std::to_string(cfg.port) + ", waiting for game data";
    else status_ = "Port " + std::to_string(cfg.port) + " in use: " + err;
    LOG("source: live, %s", status_.c_str());
}

void App::useSim()
{
    stopAll();
    source_ = Source::Sim;
    sim_ = std::make_unique<f1sim::Sim>();
    sim_->start([this](const uint8_t *p, size_t n) { hub.inject(p, n); }, cfg.simSpeed);
    status_ = "Simulation: 20 cars, Monza";
    LOG("source: virtual race, speed %.1fx", cfg.simSpeed);
}

bool App::useReplay(const std::string &path)
{
    stopAll();
    source_ = Source::Replay;
    std::string err;
    if (!replay.open(path, &err)) {
        status_ = fs::path(path).filename().string() + ": " + err;
        LOG("source: replay %s failed: %s", path.c_str(), err.c_str());
        return false;
    }
    status_ = "Replay " + fs::path(path).filename().string();
    LOG("source: replay %s", path.c_str());
    replay.play();
    return true;
}

double App::recordingSeconds() const
{
    return recording() ? hub.recordedSeconds() : 0;
}

std::string App::newRecordingPath() const
{
    std::error_code ec;
    fs::create_directories(recordingsDir(), ec);
    std::string p = recordingsDir() + "/" + (source_ == Source::Sim ? "sim-" : "") + timestampName();
    for (int k = 2; fs::exists(p); k++) // two sessions within one second
        p = recordingsDir() + "/" + (source_ == Source::Sim ? "sim-" : "") + timestampName().substr(0, 15) + "-" +
            std::to_string(k) + ".f1tr";
    return p;
}

void App::startRecording()
{
    if (!canRecord() || recording()) return;
    hub.setRecordingRotation([this] { return newRecordingPath(); });
    const std::string p = newRecordingPath();
    if (!hub.startRecording(p)) { // shown next to the record button, so a click that cannot record says why
        recordError_ = "Cannot write " + recordingsDir() + " (no permission, or blocked by antivirus?)";
        LOG("recording: FAILED to create %s", p.c_str());
        return;
    }
    recordError_.clear();
    LOG("recording: start %s", p.c_str());
}

void App::stopRecording()
{
    if (!recording()) return;
    hub.stopRecording();
    LOG("recording: stop");
    finishRecordings();
}

void App::finishRecordings()
{
    const auto done = hub.takeFinished();
    if (done.empty()) return;
    for (const auto &path : done) {
        const auto info = f1hub::scanRecording(path);
        std::error_code ec;
        if (!info.ok || info.packets == 0) { // nothing arrived while recording
            fs::remove(path, ec);
            LOG("recording: %s empty, removed", path.c_str());
            continue;
        }
        if (info.sessionType < 0) { // no Session packet: keep the time stamp name
            LOG("recording: %s closed, %llu packets, no session info", path.c_str(), (unsigned long long)info.packets);
            continue;
        }
        const fs::path p(path);
        const std::string tag = "_" + tracks::asciiName(info.trackId) + "_" + timing::sessionTypeTag(info.sessionType);
        const std::string stem = p.stem().string();
        // already named (a file closed before, carried on after a pause and closed again)
        if (stem.size() >= tag.size() && stem.compare(stem.size() - tag.size(), tag.size(), tag) == 0) {
            LOG("recording: closed %s again, %llu packets", path.c_str(), (unsigned long long)info.packets);
            continue;
        }
        fs::path to = p.parent_path() / (stem + tag + ".f1tr");
        fs::rename(p, to, ec);
        if (!ec) hub.recordingRenamed(path, to.string());
        LOG("recording: closed %s, %llu packets, track %d, session type %d%s", to.string().c_str(),
            (unsigned long long)info.packets, int(info.trackId), int(info.sessionType),
            ec ? (" (rename failed: " + ec.message() + ")").c_str() : "");
    }
    scanRecordings();
}

void App::applyTargets()
{
    for (const auto &t : cfg.targets) {
        LOG("forward: %s:%u %s", t.host.c_str(), unsigned(t.port), t.enabled ? "on" : "off");
        const bool local = t.host == "127.0.0.1" || t.host == "localhost" || t.host == "0.0.0.0";
        if (t.enabled && t.port == cfg.port && local)
            LOG("forward: WARNING %s:%u is this program's own listening port: it would loop, so the hub skips it",
                t.host.c_str(), unsigned(t.port));
    }
    hub.setTargets(cfg.targets);
    lastSent_.assign(cfg.targets.size(), 0);
    targetRate_.assign(cfg.targets.size(), 0);
}

bool App::relisten()
{
    if (source_ != Source::Live) return true;
    useLive();
    return hub.listening();
}

double App::secondsSinceData() const
{
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - lastData_).count();
}

void App::update()
{
    const auto now = std::chrono::steady_clock::now();
    const auto st = hub.stats();
    if (st.received != lastReceived_ && source_ != Source::Replay) lastData_ = now;
    if (source_ == Source::Replay && replay.playing()) lastData_ = now;
    const double dt = std::chrono::duration<double>(now - rateT_).count();
    if (dt >= 0.5) {
        rate_ = double(st.received - lastReceived_) / dt;
        for (int i = 0; i < 16; i++) {
            perIdRate_[i] = double(st.perId[i] - lastPerId_[i]) / dt;
            lastPerId_[i] = st.perId[i];
        }
        const auto ts = hub.targets();
        if (lastSent_.size() != ts.size()) { lastSent_.assign(ts.size(), 0); targetRate_.assign(ts.size(), 0); }
        for (size_t i = 0; i < ts.size(); i++) {
            targetRate_[i] = ts[i].sent >= lastSent_[i] ? double(ts[i].sent - lastSent_[i]) / dt : 0;
            lastSent_[i] = ts[i].sent;
        }
        lastReceived_ = st.received;
        rateT_ = now;
        monitor(st, now, dt);
    }

    finishRecordings(); // files closed by a session change or the end of a session (SEND)
    if (now - trackT_ > std::chrono::seconds(1)) {
        trackT_ = now;
        updateTrack();
    }

}

namespace {
const char *const PACKET_NAMES[16] = {"Motion", "Session", "LapData", "Event", "Participants", "CarSetups",
                                      "CarTelemetry", "CarStatus", "FinalClass", "Lobby", "CarDamage", "History",
                                      "TyreSets", "MotionEx", "TimeTrial", "LapPositions"};

// "Motion 60, LapData 60, ..." per second over `dt`, ids that sent nothing left out
std::string perIdText(const f1hub::Stats &a, const f1hub::Stats &b, double dt)
{
    std::string out;
    char buf[48];
    for (int i = 0; i < 16; i++) {
        const uint64_t n = a.perId[i] - b.perId[i];
        if (!n) continue;
        std::snprintf(buf, sizeof buf, "%s%s %.0f", out.empty() ? "" : ", ", PACKET_NAMES[i], double(n) / dt);
        out += buf;
    }
    return out.empty() ? "-" : out;
}

std::string sendersText(const f1hub::Stats &a, const f1hub::Stats &b, double dt)
{
    std::string out;
    char buf[96];
    for (const auto &s : a.senders) {
        uint64_t before = 0;
        for (const auto &o : b.senders)
            if (o.addr == s.addr) before = o.packets;
        if (s.packets == before) continue;
        std::snprintf(buf, sizeof buf, "%s%s %.0f/s", out.empty() ? "" : ", ", s.addr.c_str(), double(s.packets - before) / dt);
        out += buf;
    }
    return out.empty() ? "-" : out;
}
} // namespace

// Normal F1 25 traffic is a few hundred packets a second (most types at the UDP rate the game is set to, 20-120 Hz).
// Thousands a second mean something is wrong, usually a loop: a forward target that sends the packets back here.
void App::monitor(const f1hub::Stats &st, std::chrono::steady_clock::time_point now, double dt)
{
    if (st.received < monLast_.received) monLast_ = {}; // the hub was restarted
    if (!monHadData_ && st.received > 0) {
        monHadData_ = true;
        monT_ = now;
        LOG("packets: first data, format %u, game %u v%u.%02u, from %s", unsigned(st.packetFormat), unsigned(st.gameYear),
            unsigned(st.gameMajor), unsigned(st.gameMinor), st.senders.empty() ? "inside the program" : st.senders[0].addr.c_str());
        monFormat_ = st.packetFormat;
        monLast_ = st;
        return;
    }
    if (!monHadData_) return;
    if (st.packetFormat != monFormat_ && st.packetFormat) {
        LOG("packets: format changed %u -> %u (game %u v%u.%02u)", unsigned(monFormat_), unsigned(st.packetFormat),
            unsigned(st.gameYear), unsigned(st.gameMajor), unsigned(st.gameMinor));
        monFormat_ = st.packetFormat;
    }

    // silence and recovery (live only: replays and the virtual race stop on purpose)
    if (source_ == Source::Live) {
        const double quiet = secondsSinceData();
        if (!monSilent_ && quiet > 1.0) {
            monSilent_ = true;
            LOG("packets: nothing received for %.1f s", quiet);
        } else if (monSilent_ && quiet < 0.5) {
            monSilent_ = false;
            LOG("packets: data again");
        }
    }

    // too many: log at once (at most every 2 s) with the breakdown
    if (source_ == Source::Live && rate_ > 2000 && now - highLogT_ > std::chrono::seconds(2)) {
        highLogT_ = now;
        LOG("packets: HIGH RATE %.0f/s (last %.1f s): duplicates dropped %.0f/s, invalid %.0f/s; by type: %s; senders: %s; "
            "forwarded %.0f/s, send errors %.0f/s",
            rate_, dt, double(st.duplicates - monLast_.duplicates) / dt, double(st.invalid - monLast_.invalid) / dt,
            perIdText(st, monLast_, dt).c_str(), sendersText(st, monLast_, dt).c_str(),
            double(st.forwarded - monLast_.forwarded) / dt, double(st.sendErrors - monLast_.sendErrors) / dt);
    }

    // every 10 s: a summary line
    const double span = std::chrono::duration<double>(now - monT_).count();
    if (span < 10) return;
    if (st.received != monLast_.received || source_ == Source::Live) {
        LOG("packets: %.0f/s over %.0f s, duplicates dropped %.0f/s, invalid %.0f/s, forwarded %.0f/s, send errors "
            "%.0f/s, self-target skipped %.0f/s; by type: %s; senders: %s",
            double(st.received - monLast_.received) / span, span, double(st.duplicates - monLast_.duplicates) / span,
            double(st.invalid - monLast_.invalid) / span, double(st.forwarded - monLast_.forwarded) / span,
            double(st.sendErrors - monLast_.sendErrors) / span,
            double(st.selfTargetSkipped - monLast_.selfTargetSkipped) / span, perIdText(st, monLast_, span).c_str(),
            sendersText(st, monLast_, span).c_str());
        const auto ts = hub.targets();
        for (size_t i = 0; i < ts.size(); i++)
            if (ts[i].target.enabled && ts[i].errors)
                LOG("packets: forward %s:%u has %llu send errors in total", ts[i].target.host.c_str(),
                    unsigned(ts[i].target.port), (unsigned long long)ts[i].errors);
    }
    monT_ = now;
    monLast_ = st;
}

void App::updateTrack()
{
    const auto snap = race.snapshot();
    const auto &si = snap.session;
    if (!si.valid) {
        if (!snap.hasData) { trackGeo = {}; trackStatus.clear(); trackId_ = -2; }
        return;
    }
    if (snap.sessionUID != trackSession_ || si.trackId != trackId_) { // new session or track: start over
        trackSession_ = snap.sessionUID;
        trackId_ = si.trackId;
        trackFinal_ = false;
        trackGeo = {};
    }
    if (trackFinal_) return;
    // a Track N Race line (by NoGoat): in game coordinates already
    if (const tracks::TnrTrack *t = tracks::tnrForGame(si.trackId)) {
        auto g = tracks::fromTnr(*t, float(si.trackLength));
        if (g.ok) {
            trackGeo = std::move(g);
            trackStatus = trackGeo.name + " · " + timing::sessionTypeName(si.sessionType) +
                          " · line from Track N Race (not scanned: width estimated)";
            trackFinal_ = true;
            LOG("track: %s, Track N Race line, %zu points, %zu zones", trackGeo.name.c_str(), trackGeo.cx.size(),
                trackGeo.zones.size());
            return;
        }
    }
    const tracks::Shape *shape = tracks::forGame(si.trackId);
    if (!shape) {
        trackStatus = "Track id " + std::to_string(si.trackId) + " has no track data; showing the driving line";
        trackFinal_ = true;
        return;
    }
    // the fastest complete lap if there is one (final), else everything driven so far
    std::vector<tracks::DrivenPoint> pts;
    bool complete = false;
    trace.read([&](const timing::TraceData &d) {
        const timing::LapTrace *lap = d.bestLap >= 0 ? &d.laps[d.bestLap] : nullptr;
        for (auto it = d.laps.rbegin(); !lap && it != d.laps.rend(); ++it)
            if (it->complete) lap = &*it;
        if (lap) {
            complete = true;
            for (const auto &q : lap->s) pts.push_back({q.dist, q.x, q.z});
        } else {
            for (const auto &l : d.laps)
                for (const auto &q : l.s) pts.push_back({q.dist, q.x, q.z});
        }
    });
    auto g = tracks::align(*shape, pts, float(si.trackLength));
    if (g.ok) {
        trackGeo = std::move(g);
        trackFinal_ = complete;
        char b[160];
        std::snprintf(b, sizeof b, "%s · %s · data: %s · fit error %.1f m", shape->name.c_str(),
                      timing::sessionTypeName(si.sessionType), shape->source.c_str(), trackGeo.rms);
        trackStatus = b;
    } else if (complete) {
        trackStatus = shape->name + ": track data does not match the driving line (layout changed?); showing the driving line";
        trackFinal_ = true;
    } else {
        trackStatus = shape->name + ": aligning the track data after a little more driving...";
    }
}

void App::scanRecordings()
{
    static std::map<std::string, std::pair<std::time_t, RecFile>> cache; // path -> (mtime, info)
    std::vector<RecFile> out;
    std::error_code ec;
    for (const auto &e : fs::directory_iterator(recordingsDir(), ec)) {
        if (e.path().extension() != ".f1tr") continue;
        RecFile r;
        r.path = e.path().string();
        r.name = e.path().filename().string();
        auto ft = fs::last_write_time(e.path(), ec);
        r.mtime = std::chrono::system_clock::to_time_t(
            std::chrono::time_point_cast<std::chrono::system_clock::duration>(ft - fs::file_time_type::clock::now() +
                                                                               std::chrono::system_clock::now()));
        auto it = cache.find(r.path);
        if (it != cache.end() && it->second.first == r.mtime && r.path != recordingPath()) r.info = it->second.second.info;
        else {
            r.info = f1hub::scanRecording(r.path);
            cache[r.path] = {r.mtime, r};
        }
        out.push_back(r);
    }
    std::sort(out.begin(), out.end(), [](const RecFile &a, const RecFile &b) { return a.mtime > b.mtime; });
    recordings = std::move(out);
}

// ---- formatting ----------------------------------------------------------------------------------------------

std::string timestampName()
{
    std::time_t t = std::time(nullptr);
    std::tm tm{};
    localtime_s(&tm, &t);
    char b[64];
    std::strftime(b, sizeof b, "%Y%m%d-%H%M%S.f1tr", &tm);
    return b;
}

std::string formatDuration(double s)
{
    const long long t = (long long)std::max(0.0, s);
    char b[32];
    if (t >= 3600) std::snprintf(b, sizeof b, "%lld:%02lld:%02lld", t / 3600, t / 60 % 60, t % 60);
    else std::snprintf(b, sizeof b, "%lld:%02lld", t / 60, t % 60);
    return b;
}

std::string formatBytes(uint64_t b)
{
    char s[32];
    if (b >= (1ull << 30)) std::snprintf(s, sizeof s, "%.2f GB", double(b) / double(1ull << 30));
    else if (b >= (1ull << 20)) std::snprintf(s, sizeof s, "%.1f MB", double(b) / double(1ull << 20));
    else std::snprintf(s, sizeof s, "%.0f KB", double(b) / 1024.0);
    return s;
}

} // namespace app
