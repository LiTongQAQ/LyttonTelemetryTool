// f1hub: command-line front end for the hub until the dash app's UI exists.
//
//   f1hub listen [--port 20700] [--forward 20777] [--forward 127.0.0.1:20779 ...] [--record file.f1tr]
//   f1hub replay file.f1tr [--speed 1.0] [--forward 20777 ...]
#include "hub.hpp"

#include <f1tel/parse.hpp>

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

static std::atomic<bool> g_stop{false};

static void usage()
{
    std::puts("usage:\n"
              "  f1hub listen [--port 20700] [--forward [host:]port]... [--record file.f1tr]\n"
              "  f1hub replay file.f1tr [--speed 1.0] [--forward [host:]port]...\n"
              "\n"
              "Set F1 25 > Settings > Telemetry to UDP on, 127.0.0.1, the --port value, format 2025.");
}

static f1hub::Target parseTarget(const std::string &s)
{
    f1hub::Target t;
    auto colon = s.rfind(':');
    if (colon != std::string::npos) {
        t.host = s.substr(0, colon);
        t.port = uint16_t(std::atoi(s.c_str() + colon + 1));
    } else {
        t.port = uint16_t(std::atoi(s.c_str()));
    }
    return t;
}

static void printStats(const f1hub::Stats &s)
{
    std::printf("\rrx %llu  invalid %llu  fwd %llu  err %llu  | tel %llu sta %llu lap %llu dmg %llu   ",
                (unsigned long long)s.received, (unsigned long long)s.invalid, (unsigned long long)s.forwarded,
                (unsigned long long)s.sendErrors, (unsigned long long)s.perId[6], (unsigned long long)s.perId[7],
                (unsigned long long)s.perId[2], (unsigned long long)s.perId[10]);
    std::fflush(stdout);
}

int main(int argc, char **argv)
{
    if (argc < 2) { usage(); return 2; }
    std::signal(SIGINT, [](int) { g_stop = true; });

    std::string mode = argv[1], file, record;
    uint16_t port = 20700;
    double speed = 1.0;
    f1hub::Hub hub;

    int i = 2;
    if (mode == "replay") {
        if (argc < 3) { usage(); return 2; }
        file = argv[i++];
    } else if (mode != "listen") {
        usage();
        return 2;
    }
    for (; i < argc; i++) {
        std::string a = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--port") port = uint16_t(std::atoi(next().c_str()));
        else if (a == "--forward") hub.addTarget(parseTarget(next()));
        else if (a == "--record") record = next();
        else if (a == "--speed") speed = std::atof(next().c_str());
        else { usage(); return 2; }
    }

    if (mode == "replay") {
        std::thread stats([&] {
            while (!g_stop) { printStats(hub.stats()); std::this_thread::sleep_for(std::chrono::milliseconds(250)); }
        });
        long long n = f1hub::Player::play(hub, file, speed, &g_stop);
        g_stop = true;
        stats.join();
        printStats(hub.stats());
        if (n < 0) { std::printf("\n%s is not an f1hub recording\n", file.c_str()); return 1; }
        std::printf("\nreplayed %lld packets\n", n);
        return 0;
    }

    std::string err;
    if (!hub.listen(port, &err)) { std::printf("%s\n", err.c_str()); return 1; }
    if (!record.empty() && !hub.startRecording(record)) { std::printf("cannot write %s\n", record.c_str()); return 1; }
    std::printf("listening on UDP %u%s, Ctrl+C to stop\n", port, record.empty() ? "" : (", recording to " + record).c_str());
    while (!g_stop) { printStats(hub.stats()); std::this_thread::sleep_for(std::chrono::milliseconds(250)); }
    hub.stop();
    hub.stopRecording();
    std::puts("");
    return 0;
}
