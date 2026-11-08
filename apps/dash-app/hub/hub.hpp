// Telemetry hub: the one place that receives F1 25 UDP telemetry.
//
// It listens on one port, hands every datagram to in-process subscribers (VR dash, replay view), forwards a copy to
// each configured UDP target (tools that want their own port, mods hard-wired to 20777) and can record everything to
// a file. A Player feeds a recording back through the same path, so subscribers and targets can't tell the difference.
#pragma once

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace f1hub {

struct Target {
    std::string host = "127.0.0.1";
    uint16_t port = 0;
    bool enabled = true;
    std::string note; // what listens there, shown in the UI
};

struct TargetStats {
    Target target;
    bool resolved = false; // host parsed as an IPv4 address
    uint64_t sent = 0, errors = 0;
};

struct Stats {
    uint64_t received = 0;    // datagrams in (live or replay)
    uint64_t invalid = 0;     // not a valid F1 25 packet (still forwarded)
    uint64_t forwarded = 0;   // datagrams sent out, summed over targets
    uint64_t sendErrors = 0;
    uint64_t perId[16] = {};  // valid packets by packet id
    uint64_t duplicates = 0;  // byte-identical to one of the last 256 packets: a forwarding loop or a double sender.
                              // Dropped: not recorded, forwarded or passed on (the game never repeats a packet)
    uint64_t selfTargetSkipped = 0; // sends not made because the target is this hub's own port (would loop)
    // who sends to the listening port (UDP only; the virtual race and replays inject directly)
    struct Sender {
        std::string addr; // "ip:port"
        uint64_t packets = 0;
    };
    std::vector<Sender> senders;
    // the last valid header's game version
    uint16_t packetFormat = 0;
    uint8_t gameYear = 0, gameMajor = 0, gameMinor = 0;
};

// Called on the hub's receive thread. Keep it short: copy what you need and return.
using Subscriber = std::function<void(const uint8_t *data, size_t len)>;

// Recording file: "F1TR" magic, u16 version (1), u16 reserved, then records of
// { u64 nanoseconds since recording start, u16 length, length bytes }. All little-endian.
// Opens a file by a path string as the program keeps them (std::filesystem's narrow form), so folders with Chinese
// or other non-ASCII names work (plain fopen would read the string in the old system code page).
std::FILE *openFile(const std::string &path, const wchar_t *mode);

class Recorder {
public:
    bool open(const std::string &path);
    bool openAppend(const std::string &path); // an existing recording, to carry on at its end
    void write(uint64_t tNs, const uint8_t *data, size_t len);
    void close();
    bool isOpen() const { return f_ != nullptr; }
    uint64_t size() const { return size_; }  // bytes in the file so far
    bool truncate(uint64_t size);           // drop everything after `size` bytes (a flashback)
    ~Recorder() { close(); }

private:
    std::FILE *f_ = nullptr;
    uint64_t size_ = 0;
    std::mutex m_;
};

class Hub {
public:
    Hub();
    ~Hub();

    void addTarget(Target t);
    // Replaces all forward targets at once (counters restart). Safe while running.
    void setTargets(const std::vector<Target> &targets);
    std::vector<TargetStats> targets() const;
    int subscribe(Subscriber s); // returns an id for unsubscribe
    void unsubscribe(int id);

    // Starts receiving on the given port (all local interfaces). False if the port is taken.
    bool listen(uint16_t port, std::string *error = nullptr);
    void stop();

    bool listening() const;

    bool startRecording(const std::string &path);
    // While recording, a packet from a new session closes the file and continues in the one `next` names, so every
    // session (practice, qualifying, race ...) gets its own file. Closed files are collected for takeFinished().
    void setRecordingRotation(std::function<std::string()> next);
    std::vector<std::string> takeFinished();
    std::string recordingPath() const;
    void stopRecording();
    bool recording() const; // switched on (possibly between sessions, with no file open)
    // Call now and then: the recording's session silent for `after` (left for the menus, or paused) closes its file,
    // so it shows up in the library. If the same session carries on (it was a pause), the file is opened again and
    // the packets go on at its end; another session opens a new file.
    void checkRecordingSilence(std::chrono::seconds after);
    // The program renamed a closed recording: a session carrying on in it must find it under the new name.
    void recordingRenamed(const std::string &from, const std::string &to);
    uint64_t recordedBytes() const;
    // A recording pauses by itself while the game is paused or sends nothing: no packets for a second, or packets
    // whose session time has not moved for 0.3 s. Nothing is written while paused and the pause leaves no gap in the file.
    bool recordingPaused() const;
    double recordedSeconds() const; // recorded time, pauses excluded

    // Entry point for every datagram, live or replayed. A datagram identical to a recent one is counted and dropped.
    // forward = false: recorded and passed to subscribers, but not sent to the targets (a replay catching up after a
    // seek would otherwise send minutes of packets to them at once)
    void inject(const uint8_t *data, size_t len, bool forward = true);

    Stats stats() const;

private:
    void receiveLoop();
    uint64_t nowNs() const;

    struct Impl;
    std::unique_ptr<Impl> d_;
};

// Replays a recording into a hub with the original timing.
class Player {
public:
    // speed 1.0 = real time; 0 = as fast as possible. Returns packets played, or -1 if the file is not a recording.
    static long long play(Hub &hub, const std::string &path, double speed = 1.0,
                          const std::atomic<bool> *cancel = nullptr);
};

} // namespace f1hub
