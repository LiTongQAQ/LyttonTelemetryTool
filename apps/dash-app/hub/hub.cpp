#include "hub.hpp"

#include <f1tel/parse.hpp>

#include <winsock2.h>
#include <ws2tcpip.h>

#include <chrono>
#include <cstring>
#include <filesystem>
#include <io.h>
#include <map>

namespace f1hub {

namespace {

struct WsaInit {
    bool ok;
    WsaInit()
    {
        WSADATA w;
        ok = WSAStartup(MAKEWORD(2, 2), &w) == 0;
    }
    ~WsaInit()
    {
        if (ok) WSACleanup();
    }
};

const char MAGIC[4] = {'F', '1', 'T', 'R'};
constexpr uint16_t VERSION = 1;

} // namespace

// ---- Recorder ------------------------------------------------------------------------------------------------

std::FILE *openFile(const std::string &path, const wchar_t *mode)
{
    const std::wstring w = std::filesystem::path(path).wstring();
    return _wfopen(w.c_str(), mode);
}

bool Recorder::open(const std::string &path)
{
    std::lock_guard<std::mutex> l(m_);
    if (f_) std::fclose(f_);
    f_ = openFile(path, L"wb");
    if (!f_) return false;
    uint16_t v = VERSION, reserved = 0;
    std::fwrite(MAGIC, 1, 4, f_);
    std::fwrite(&v, 2, 1, f_);
    std::fwrite(&reserved, 2, 1, f_);
    size_ = 8;
    return true;
}

bool Recorder::openAppend(const std::string &path)
{
    std::lock_guard<std::mutex> l(m_);
    if (f_) std::fclose(f_);
    f_ = openFile(path, L"ab");
    if (!f_) return false;
    _fseeki64(f_, 0, SEEK_END);
    size_ = uint64_t(_ftelli64(f_));
    return true;
}

void Recorder::write(uint64_t tNs, const uint8_t *data, size_t len)
{
    std::lock_guard<std::mutex> l(m_);
    if (!f_ || len > 0xffff) return;
    uint16_t n = uint16_t(len);
    std::fwrite(&tNs, 8, 1, f_);
    std::fwrite(&n, 2, 1, f_);
    std::fwrite(data, 1, len, f_);
    size_ += 10 + len;
}

bool Recorder::truncate(uint64_t size)
{
    std::lock_guard<std::mutex> l(m_);
    if (!f_ || size < 8 || size > size_) return false;
    std::fflush(f_);
    if (_chsize_s(_fileno(f_), int64_t(size)) != 0) return false;
    _fseeki64(f_, int64_t(size), SEEK_SET);
    size_ = size;
    return true;
}

void Recorder::close()
{
    std::lock_guard<std::mutex> l(m_);
    if (f_) std::fclose(f_);
    f_ = nullptr;
}

// ---- Hub -----------------------------------------------------------------------------------------------------

struct Hub::Impl {
    WsaInit wsa;
    SOCKET rx = INVALID_SOCKET;
    SOCKET tx = INVALID_SOCKET;
    std::thread thread;
    std::atomic<bool> running{false};

    std::mutex m; // guards targets, subscribers
    // duplicate detection: hashes of the last packets (guarded by sm)
    uint64_t recent[256] = {};
    unsigned recentPos = 0;
    std::map<uint64_t, Stats::Sender> senders; // key = ip << 16 | port (guarded by sm)
    std::atomic<uint16_t> listenPort{0};        // 0 = not listening
    struct Out {
        TargetStats st;
        sockaddr_in addr{};
    };
    std::vector<Out> targets;
    std::atomic<uint64_t> recBytes{0};
    std::map<int, Subscriber> subs;
    int nextSub = 1;

    Recorder rec;
    std::chrono::steady_clock::time_point recStart;
    // recording clock without the pauses
    std::mutex rm;
    uint64_t recNs = 0;                                  // timestamp of the last written packet
    std::chrono::steady_clock::time_point recLastWall;   // when it was written
    float recLastSession = -1;                           // its session time
    std::chrono::steady_clock::time_point sessionMoved;  // when the session time last advanced
    // flashbacks: where the file stood at each 0.1 s of session time, to cut it back when the game goes back in time
    // (as the old Project_F1-25 dropped the abandoned part from its saved session); the recording then holds only
    // the timeline that counted
    struct Cut { float st; uint64_t size, ns; };
    std::vector<Cut> cuts;
    unsigned flashbacks = 0;
    std::atomic<bool> recPaused{false};
    std::string recPath;
    uint64_t recUid = 0;
    uint64_t recEndedUid = 0; // the game said this session ended (SEND): nothing more goes into its file
    // recording switched on; between sessions (after SEND, or the game silent after leaving a session) no file is
    // open and the next session's first packet opens one
    std::atomic<bool> recArmed{false};
    std::chrono::steady_clock::time_point recUidSeen; // last packet of the recording's session
    // the file closed because its session went silent: where it is and where its clock stopped, to carry on in it
    uint64_t pausedUid = 0, pausedNs = 0;
    std::string pausedPath;
    std::function<std::string()> rotate;
    std::vector<std::string> finished;

    mutable std::mutex sm;
    Stats st;
};

Hub::Hub() : d_(std::make_unique<Impl>())
{
    d_->tx = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
}

Hub::~Hub()
{
    stop();
    if (d_->tx != INVALID_SOCKET) closesocket(d_->tx);
}

void Hub::addTarget(Target t)
{
    Impl::Out o;
    o.st.target = t;
    o.addr.sin_family = AF_INET;
    o.addr.sin_port = htons(t.port);
    o.st.resolved = t.port && inet_pton(AF_INET, t.host.c_str(), &o.addr.sin_addr) == 1;
    std::lock_guard<std::mutex> l(d_->m);
    d_->targets.push_back(o);
}

void Hub::setTargets(const std::vector<Target> &targets)
{
    {
        std::lock_guard<std::mutex> l(d_->m);
        d_->targets.clear();
    }
    for (const auto &t : targets) addTarget(t);
}

std::vector<TargetStats> Hub::targets() const
{
    std::lock_guard<std::mutex> l(d_->m);
    std::vector<TargetStats> v;
    for (const auto &o : d_->targets) v.push_back(o.st);
    return v;
}

bool Hub::listening() const { return d_->running; }
bool Hub::recording() const { return d_->recArmed; }

bool Hub::recordingPaused() const
{
    if (!d_->recArmed) return false;
    if (!d_->rec.isOpen()) return true; // between sessions
    std::lock_guard<std::mutex> l(d_->rm);
    return d_->recPaused || std::chrono::steady_clock::now() - d_->recLastWall > std::chrono::seconds(1);
}

void Hub::setRecordingRotation(std::function<std::string()> next)
{
    std::lock_guard<std::mutex> l(d_->rm);
    d_->rotate = std::move(next);
}

std::vector<std::string> Hub::takeFinished()
{
    std::lock_guard<std::mutex> l(d_->rm);
    return std::move(d_->finished);
}

std::string Hub::recordingPath() const
{
    std::lock_guard<std::mutex> l(d_->rm);
    return d_->rec.isOpen() ? d_->recPath : std::string();
}

double Hub::recordedSeconds() const
{
    std::lock_guard<std::mutex> l(d_->rm);
    return d_->rec.isOpen() ? double(d_->recNs) / 1e9 : 0.0;
}
uint64_t Hub::recordedBytes() const { return d_->recBytes; }

int Hub::subscribe(Subscriber s)
{
    std::lock_guard<std::mutex> l(d_->m);
    int id = d_->nextSub++;
    d_->subs[id] = std::move(s);
    return id;
}

void Hub::unsubscribe(int id)
{
    std::lock_guard<std::mutex> l(d_->m);
    d_->subs.erase(id);
}

bool Hub::listen(uint16_t port, std::string *error)
{
    stop();
    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET) {
        if (error) *error = "socket() failed: " + std::to_string(WSAGetLastError());
        return false;
    }
    // Exclusive: two programs on one port silently split the packets between them, which is what the hub avoids.
    BOOL yes = TRUE;
    setsockopt(s, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char *>(&yes), sizeof yes);
    int buf = 1 << 20;
    setsockopt(s, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char *>(&buf), sizeof buf);
    DWORD timeoutMs = 200; // lets stop() end the loop
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char *>(&timeoutMs), sizeof timeoutMs);

    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons(port);
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(s, reinterpret_cast<sockaddr *>(&a), sizeof a) != 0) {
        if (error) *error = "UDP port " + std::to_string(port) + " is in use (error " + std::to_string(WSAGetLastError()) + ")";
        closesocket(s);
        return false;
    }
    d_->rx = s;
    d_->listenPort = port;
    d_->running = true;
    d_->thread = std::thread([this] { receiveLoop(); });
    return true;
}

void Hub::stop()
{
    d_->running = false;
    d_->listenPort = 0;
    if (d_->thread.joinable()) d_->thread.join();
    if (d_->rx != INVALID_SOCKET) closesocket(d_->rx);
    d_->rx = INVALID_SOCKET;
}

void Hub::receiveLoop()
{
    uint8_t buf[2048];
    while (d_->running) {
        sockaddr_in from{};
        int fromLen = sizeof from;
        int n = recvfrom(d_->rx, reinterpret_cast<char *>(buf), sizeof buf, 0, reinterpret_cast<sockaddr *>(&from), &fromLen);
        if (n <= 0) continue;
        {
            std::lock_guard<std::mutex> l(d_->sm);
            const uint64_t key = (uint64_t(ntohl(from.sin_addr.s_addr)) << 16) | ntohs(from.sin_port);
            auto &snd = d_->senders[key];
            if (snd.addr.empty()) {
                char ip[INET_ADDRSTRLEN] = {};
                inet_ntop(AF_INET, &from.sin_addr, ip, sizeof ip);
                snd.addr = std::string(ip) + ":" + std::to_string(ntohs(from.sin_port));
            }
            snd.packets++;
        }
        inject(buf, size_t(n));
    }
}

uint64_t Hub::nowNs() const
{
    return uint64_t(
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - d_->recStart).count());
}

bool Hub::startRecording(const std::string &path)
{
    {
        std::lock_guard<std::mutex> l(d_->rm);
        d_->recStart = d_->recLastWall = d_->sessionMoved = std::chrono::steady_clock::now();
        d_->recNs = 0;
        d_->recLastSession = -1;
                    d_->cuts.clear();
        d_->recPaused = false;
        d_->recPath = path;
        d_->recUid = d_->recEndedUid = 0;
        d_->recUidSeen = d_->recStart;
        d_->pausedUid = 0;
        d_->pausedPath.clear();
    }
    d_->recBytes = 8;
    d_->recArmed = d_->rec.open(path);
    return d_->recArmed;
}

void Hub::stopRecording()
{
    std::lock_guard<std::mutex> l(d_->rm);
    d_->recArmed = false;
    if (!d_->rec.isOpen()) return;
    d_->rec.close();
    d_->finished.push_back(d_->recPath);
    d_->recPath.clear();
}

void Hub::checkRecordingSilence(std::chrono::seconds after)
{
    std::lock_guard<std::mutex> l(d_->rm);
    if (!d_->recArmed || !d_->rec.isOpen() || !d_->recUid) return;
    if (std::chrono::steady_clock::now() - d_->recUidSeen < after) return;
    // left the session, or a long pause: close the file (it shows up in the library); the same session carrying on
    // reopens it
    d_->rec.close();
    d_->finished.push_back(d_->recPath);
    d_->pausedUid = d_->recUid;
    d_->pausedNs = d_->recNs;
    d_->pausedPath = d_->recPath;
    d_->recPath.clear();
    d_->recUid = 0;
    d_->recEndedUid = 0;
}

void Hub::recordingRenamed(const std::string &from, const std::string &to)
{
    std::lock_guard<std::mutex> l(d_->rm);
    if (d_->pausedPath == from) d_->pausedPath = to;
}

void Hub::inject(const uint8_t *data, size_t len, bool forward)
{
    auto h = f1tel::read_header(data, len);
    {
        std::lock_guard<std::mutex> l(d_->sm);
        d_->st.received++;
        if (h) {
            d_->st.perId[h.header.packetId]++;
            d_->st.packetFormat = h.header.packetFormat;
            d_->st.gameYear = h.header.gameYear;
            d_->st.gameMajor = h.header.gameMajorVersion;
            d_->st.gameMinor = h.header.gameMinorVersion;
        } else {
            d_->st.invalid++;
        }
        uint64_t hash = 1469598103934665603ull ^ len; // FNV-1a over the whole datagram
        for (size_t i = 0; i < len; i++) hash = (hash ^ data[i]) * 1099511628211ull;
        for (uint64_t r : d_->recent)
            if (r == hash) { d_->st.duplicates++; return; } // a loop would otherwise multiply it again
        d_->recent[d_->recentPos++ & 255] = hash;
    }

    if (d_->recArmed) {
        const auto now = std::chrono::steady_clock::now();
        bool write = true, closeAfter = false;
        uint64_t t;
        {
            std::lock_guard<std::mutex> l(d_->rm);
            // between sessions: the first packet of a session (not the one that just ended) opens the next file
            if (!d_->rec.isOpen()) {
                if (!(h && h.header.sessionUID && h.header.sessionUID != d_->recEndedUid && d_->rotate)) return;
                const bool same = h.header.sessionUID == d_->pausedUid && !d_->pausedPath.empty();
                if (same && d_->rec.openAppend(d_->pausedPath)) { // a pause: carry on in the same file
                    d_->recPath = d_->pausedPath;
                    d_->recNs = d_->pausedNs;
                } else {
                    d_->recPath = d_->rotate();
                    if (!d_->rec.open(d_->recPath)) return;
                    d_->recNs = 0;
                    d_->recBytes = 8;
                }
                d_->pausedUid = 0;
                d_->pausedPath.clear();
                d_->recUid = h.header.sessionUID;
                d_->recEndedUid = 0;
                d_->recLastWall = d_->sessionMoved = d_->recUidSeen = now;
                d_->recLastSession = -1;
                    d_->cuts.clear();
            }
            // a new session: close this file and carry on in a new one
            if (h && h.header.sessionUID) {
                if (!d_->recUid) d_->recUid = h.header.sessionUID;
                else if (h.header.sessionUID != d_->recUid && d_->rotate) {
                    d_->finished.push_back(d_->recPath);
                    d_->recPath = d_->rotate();
                    d_->rec.open(d_->recPath); // closes the old file
                    d_->recUid = h.header.sessionUID;
                    d_->recNs = 0;
                    d_->recLastWall = d_->sessionMoved = now;
                    d_->recLastSession = -1;
                    d_->cuts.clear();
                    d_->recBytes = 8;
                }
            }
            if (h && h.header.sessionUID == d_->recUid) d_->recUidSeen = now;
            // game paused: packets keep coming but the session time stands still
            if (h) {
                const float st = h.header.sessionTime;
                if (st != d_->recLastSession) d_->sessionMoved = now;
                if (h.header.sessionUID == d_->recUid && d_->recLastSession >= 0 && st < d_->recLastSession - 0.5f) {
                    // a flashback: cut the file back to the new time
                    auto &c = d_->cuts;
                    while (!c.empty() && c.back().st > st) c.pop_back();
                    if (!c.empty() && d_->rec.truncate(c.back().size)) {
                        d_->recNs = c.back().ns;
                        d_->recBytes = c.back().size;
                        d_->flashbacks++;
                    }
                } else if (h.header.sessionUID == d_->recUid &&
                           (d_->cuts.empty() || st >= d_->cuts.back().st + 0.1f)) {
                    d_->cuts.push_back({st, d_->rec.size(), d_->recNs}); // before this packet goes in
                }
                d_->recLastSession = st;
            }
            d_->recPaused = now - d_->sessionMoved > std::chrono::milliseconds(300) ||
                            (h && h.header.sessionUID == d_->recEndedUid);
            write = !d_->recPaused;
            // the session ended (SEND, after the results): the event goes in, the rest of that session stays out of the
            // file; the next session opens a new one (the chequered flag is not the end: your last lap is still running)
            if (h && h.header.packetId == uint8_t(f1tel::PacketId::Event) && len >= sizeof(f1tel::PacketEvent) &&
                std::memcmp(data + sizeof(f1tel::PacketHeader), "SEND", 4) == 0) {
                d_->recEndedUid = h.header.sessionUID;
                closeAfter = true; // the file ends with this event
                write = true;
            }
            const auto gap = now - d_->recLastWall;
            // after a pause or a silence the clock carries on from where it stopped
            const auto step = gap > std::chrono::seconds(1) ? std::chrono::milliseconds(16) : gap;
            if (write) {
                d_->recNs += uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(step).count());
                d_->recLastWall = now;
            }
            t = d_->recNs;
        }
        if (write) {
            d_->rec.write(t, data, len);
            d_->recBytes += 10 + len;
        }
        if (closeAfter) {
            std::lock_guard<std::mutex> l(d_->rm);
            if (d_->rec.isOpen()) {
                d_->rec.close();
                d_->finished.push_back(d_->recPath);
                d_->recPath.clear();
                d_->recUid = 0;
            }
        }
    }

    uint64_t sent = 0, errors = 0, self = 0;
    const uint16_t own = d_->listenPort;
    {
        std::lock_guard<std::mutex> l(d_->m);
        for (auto &t : d_->targets) {
            if (!forward) break;
            if (!t.st.target.enabled || !t.st.resolved) continue;
            const uint32_t ip = ntohl(t.addr.sin_addr.s_addr);
            if (own && ntohs(t.addr.sin_port) == own && (ip >> 24 == 127 || ip == 0)) { self++; continue; }
            int r = sendto(d_->tx, reinterpret_cast<const char *>(data), int(len), 0,
                           reinterpret_cast<const sockaddr *>(&t.addr), sizeof t.addr);
            if (r == int(len)) { sent++; t.st.sent++; }
            else { errors++; t.st.errors++; }
        }
        for (auto &[id, s] : d_->subs) s(data, len);
    }

    std::lock_guard<std::mutex> l(d_->sm);
    d_->st.forwarded += sent;
    d_->st.sendErrors += errors;
    d_->st.selfTargetSkipped += self;
}

Stats Hub::stats() const
{
    std::lock_guard<std::mutex> l(d_->sm);
    Stats s = d_->st;
    for (const auto &[k, v] : d_->senders) s.senders.push_back(v);
    return s;
}

// ---- Player --------------------------------------------------------------------------------------------------

long long Player::play(Hub &hub, const std::string &path, double speed, const std::atomic<bool> *cancel)
{
    std::FILE *f = openFile(path, L"rb");
    if (!f) return -1;
    char magic[4];
    uint16_t version = 0, reserved = 0;
    if (std::fread(magic, 1, 4, f) != 4 || std::memcmp(magic, MAGIC, 4) != 0 || std::fread(&version, 2, 1, f) != 1 ||
        std::fread(&reserved, 2, 1, f) != 1 || version != VERSION) {
        std::fclose(f);
        return -1;
    }

    auto start = std::chrono::steady_clock::now();
    uint8_t buf[0x10000];
    long long count = 0;
    uint64_t t;
    uint16_t n;
    while ((!cancel || !*cancel) && std::fread(&t, 8, 1, f) == 1 && std::fread(&n, 2, 1, f) == 1 &&
           std::fread(buf, 1, n, f) == n) {
        if (speed > 0) {
            auto due = start + std::chrono::nanoseconds(uint64_t(double(t) / speed));
            std::this_thread::sleep_until(due);
        }
        hub.inject(buf, n);
        count++;
    }
    std::fclose(f);
    return count;
}

} // namespace f1hub
