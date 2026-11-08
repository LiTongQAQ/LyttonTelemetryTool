// End-to-end: send real packets to a listening hub over UDP, check subscribers, forwarding, record and replay.
#include "hub.hpp"
#include "replay.hpp"

#include <cstring>
#include <filesystem>

#include <f1tel/parse.hpp>

#include <winsock2.h>
#include <ws2tcpip.h>

#include <atomic>
#include <functional>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>
#include <vector>

static int failures = 0;
#define CHECK(cond)                                                                                                    \
    do {                                                                                                               \
        if (!(cond)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); failures++; }                       \
    } while (0)

static std::vector<char> load(const char *name)
{
    std::ifstream f(std::string(F1TEL_TEST_DATA) + "/" + name, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), {}};
}

static void sendTo(uint16_t port, const std::vector<char> &b)
{
    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons(port);
    inet_pton(AF_INET, "127.0.0.1", &a.sin_addr);
    sendto(s, b.data(), int(b.size()), 0, reinterpret_cast<sockaddr *>(&a), sizeof a);
    closesocket(s);
}

static void waitFor(const std::function<bool()> &cond)
{
    for (int i = 0; i < 100 && !cond(); i++) std::this_thread::sleep_for(std::chrono::milliseconds(10));
}

int main()
{
    const uint16_t kInPort = 29700, kOutPort = 29777;
    const std::string rec = "test_hub.f1tr";
    auto tel = load("CarTelemetry.bin"), sta = load("CarStatus.bin");

    // A downstream tool on kOutPort, listened to by a second hub.
    f1hub::Hub downstream;
    std::atomic<int> downstreamGot{0};
    downstream.subscribe([&](const uint8_t *, size_t) { downstreamGot++; });
    CHECK(downstream.listen(kOutPort));

    {
        f1hub::Hub hub;
        hub.addTarget({"127.0.0.1", kOutPort});
        std::atomic<int> speedSeen{-1};
        hub.subscribe([&](const uint8_t *d, size_t n) {
            if (auto p = f1tel::parse<f1tel::PacketCarTelemetry>(d, n)) speedSeen = p->car[p->header.playerCarIndex].speed;
        });
        std::string err;
        CHECK(hub.listen(kInPort, &err));

        f1hub::Hub clash; // a second program on the same port must be refused, not share packets
        CHECK(!clash.listen(kInPort));

        CHECK(hub.startRecording(rec));
        sendTo(kInPort, tel);
        sendTo(kInPort, sta);
        sendTo(kInPort, std::vector<char>(10, 0)); // junk: counted invalid, still forwarded
        waitFor([&] { return hub.stats().received == 3 && downstreamGot == 3; });
        hub.stopRecording();

        auto s = hub.stats();
        CHECK(s.received == 3);
        CHECK(s.invalid == 1);
        CHECK(s.perId[6] == 1 && s.perId[7] == 1);
        CHECK(s.forwarded == 3);
        CHECK(downstreamGot == 3);
        CHECK(speedSeen >= 0);
        std::printf("live: rx %llu fwd %llu, player speed %d km/h\n", (unsigned long long)s.received,
                    (unsigned long long)s.forwarded, speedSeen.load());
    }

    {
        f1hub::Hub replay;
        std::atomic<int> got{0};
        replay.subscribe([&](const uint8_t *, size_t) { got++; });
        long long n = f1hub::Player::play(replay, rec, 0);
        CHECK(n == 3);
        CHECK(got == 3);
        CHECK(replay.stats().perId[6] == 1);
        CHECK(f1hub::Player::play(replay, std::string(F1TEL_TEST_DATA) + "/Event.bin", 0) == -1);
    }

    std::remove(rec.c_str());
    // recording pauses while the session time stands still and leaves no gap in the file
    {
        auto pkt = load("CarTelemetry.bin");
        auto setTime = [&](float t) { std::memcpy(pkt.data() + 15, &t, 4); }; // header.sessionTime
        f1hub::Hub hub;
        const std::string path = (std::filesystem::temp_directory_path() / "f1hub_pause.f1tr").string();
        CHECK(hub.startRecording(path));
        setTime(10.f);
        hub.inject(reinterpret_cast<const uint8_t *>(pkt.data()), pkt.size());
        for (int i = 0; i < 15; i++) { // game paused: same session time for 1.5 s
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            hub.inject(reinterpret_cast<const uint8_t *>(pkt.data()), pkt.size());
        }
        CHECK(hub.recordingPaused());
        setTime(10.1f);
        hub.inject(reinterpret_cast<const uint8_t *>(pkt.data()), pkt.size());
        CHECK(!hub.recordingPaused());
        CHECK(hub.recordedSeconds() < 1.2); // the pause is not in the recording
        hub.stopRecording();
        auto info = f1hub::scanRecording(path);
        CHECK(info.ok && info.packets < 16 && info.durationNs < 1200000000ull);
        std::printf("pause: %llu packets kept, %.2f s\n", (unsigned long long)info.packets, double(info.durationNs) / 1e9);
        std::filesystem::remove(path);
    }

    // a flashback (session time jumps back) cuts the abandoned part out of the file
    {
        auto pkt = load("CarTelemetry.bin");
        f1hub::Hub hub;
        const std::string path = (std::filesystem::temp_directory_path() / "f1hub_flbk.f1tr").string();
        CHECK(hub.startRecording(path));
        uint32_t frame = 0;
        auto send = [&](float t) {
            std::memcpy(pkt.data() + 15, &t, 4);
            frame++;
            std::memcpy(pkt.data() + 19, &frame, 4);
            hub.inject(reinterpret_cast<const uint8_t *>(pkt.data()), pkt.size());
        };
        for (int k = 0; k <= 20; k++) send(10.f + 0.5f * float(k)); // 10.0 .. 20.0
        for (int k = 0; k < 4; k++) send(15.f + 0.5f * float(k));   // went back to 15.0: 15.0 .. 16.5
        hub.stopRecording();
        // kept: 10.0 .. 14.5 (10 packets) before the cut, then the 4 after it
        const auto info = f1hub::scanRecording(path);
        std::printf("flashback: %llu packets kept\n", (unsigned long long)info.packets);
        CHECK(info.packets == 14);
        std::filesystem::remove(path);
    }

    // a new session while recording closes the file and continues in a new one
    {
        auto pkt = load("CarTelemetry.bin");
        auto setUid = [&](uint64_t u) { std::memcpy(pkt.data() + 7, &u, 8); }; // header.sessionUID
        const auto dir = std::filesystem::temp_directory_path();
        int n = 0;
        f1hub::Hub hub;
        hub.setRecordingRotation([&] { return (dir / ("f1hub_rot" + std::to_string(++n) + ".f1tr")).string(); });
        const std::string first = (dir / "f1hub_rot0.f1tr").string();
        CHECK(hub.startRecording(first));
        uint32_t frame = 0; // the game numbers its frames: identical datagrams would be dropped as a loop
        auto send = [&] {
            frame++;
            std::memcpy(pkt.data() + 19, &frame, 4); // header.frameIdentifier
            hub.inject(reinterpret_cast<const uint8_t *>(pkt.data()), pkt.size());
        };
        setUid(111);
        for (int k = 0; k < 3; k++) send();
        setUid(222);
        for (int k = 0; k < 2; k++) send();
        const std::string second = hub.recordingPath();
        CHECK(second != first && !second.empty());
        hub.stopRecording();
        auto done = hub.takeFinished();
        CHECK(done.size() == 2 && done[0] == first && done[1] == second);
        CHECK(f1hub::scanRecording(first).packets == 3);
        CHECK(f1hub::scanRecording(second).packets == 2);
        for (const auto &f : done) std::filesystem::remove(f);
    }

    // the session's end (SEND) closes the file at once; the rest of that session stays out, the next opens a new file
    {
        auto pkt = load("CarTelemetry.bin");
        auto setUid = [&](std::vector<char> &p, uint64_t u) { std::memcpy(p.data() + 7, &u, 8); };
        std::vector<char> send(sizeof(f1tel::PacketEvent), 0);
        std::memcpy(send.data(), pkt.data(), sizeof(f1tel::PacketHeader));
        send[6] = char(f1tel::PacketId::Event);
        std::memcpy(send.data() + sizeof(f1tel::PacketHeader), "SEND", 4);
        const auto dir = std::filesystem::temp_directory_path();
        int n = 0;
        f1hub::Hub hub;
        hub.setRecordingRotation([&] { return (dir / ("f1hub_end" + std::to_string(++n) + ".f1tr")).string(); });
        const std::string first = (dir / "f1hub_end0.f1tr").string();
        CHECK(hub.startRecording(first));
        uint32_t frame = 0;
        auto inject = [&](std::vector<char> &p) {
            frame++;
            std::memcpy(p.data() + 19, &frame, 4);
            hub.inject(reinterpret_cast<const uint8_t *>(p.data()), p.size());
        };
        setUid(pkt, 111);
        setUid(send, 111);
        for (int k = 0; k < 3; k++) inject(pkt);
        inject(send);
        auto done = hub.takeFinished();
        CHECK(done.size() == 1 && done[0] == first); // closed by SEND, without waiting for the next session
        CHECK(hub.recording() && hub.recordingPath().empty());
        CHECK(f1hub::scanRecording(first).packets == 4);
        for (int k = 0; k < 2; k++) inject(pkt); // the ended session's results screen: not recorded
        CHECK(hub.recordingPath().empty());
        setUid(pkt, 222);
        for (int k = 0; k < 2; k++) inject(pkt);
        const std::string second = hub.recordingPath();
        CHECK(!second.empty() && second != first);
        hub.stopRecording();
        done = hub.takeFinished();
        CHECK(done.size() == 1 && done[0] == second);
        CHECK(f1hub::scanRecording(second).packets == 2);
        std::filesystem::remove(first);
        std::filesystem::remove(second);
    }

    // a folder with a Chinese name (a user installed to E:\F1遥测): the recording opens, is written and scans back
    {
        auto pkt = load("CarTelemetry.bin");
        const auto dir = std::filesystem::temp_directory_path() / std::filesystem::u8path("F1遥测测试");
        std::filesystem::create_directories(dir);
        const std::string path = (dir / "rec.f1tr").string();
        f1hub::Recorder rec;
        CHECK(rec.open(path));
        rec.write(0, reinterpret_cast<const uint8_t *>(pkt.data()), pkt.size());
        rec.close();
        CHECK(f1hub::scanRecording(path).packets == 1);
        std::filesystem::remove_all(dir);
    }

    // loops: a repeated datagram is dropped, and a target on the hub's own port is never sent to
    {
        auto pkt = load("CarTelemetry.bin");
        f1hub::Hub hub;
        CHECK(hub.listen(kInPort + 5));
        hub.addTarget({"127.0.0.1", uint16_t(kInPort + 5)});
        int seen = 0;
        hub.subscribe([&](const uint8_t *, size_t) { seen++; });
        for (int k = 0; k < 3; k++) hub.inject(reinterpret_cast<const uint8_t *>(pkt.data()), pkt.size());
        const auto st = hub.stats();
        CHECK(seen == 1 && st.duplicates == 2);
        CHECK(st.forwarded == 0 && st.selfTargetSkipped == 1);
        hub.stop();
    }

    std::printf(failures ? "%d FAILED\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
