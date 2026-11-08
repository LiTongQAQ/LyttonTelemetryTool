// Virtual telemetry: a 20-car race on an invented 5.2 km circuit, sent as real F1 25 UDP packets (Participants,
// LapData, CarTelemetry, CarStatus, CarDamage, SessionHistory) with the exact layouts from packets.hpp. Everything
// downstream — hub, recording, replay, forwarding, RaceState — runs exactly as it would with the game.
//
// The physics is a speed profile from braking / acceleration curves around ten corners; each car drives it at its
// own pace with a little lap-to-lap noise, so gaps, overtakes, purple and green sectors and invalid laps all occur.
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <thread>

namespace f1sim {

using Out = std::function<void(const uint8_t *data, size_t len)>;

class Sim {
public:
    explicit Sim(uint64_t seed = 0); // 0 = random session
    ~Sim();

    // Advances the race by dt seconds of game time and sends the packets that fall into it (60 Hz telemetry).
    void step(double dt, const Out &out);

    // Runs step() in real time on a thread.
    void start(Out out, double speed = 1.0);
    void stop();
    bool running() const { return running_; }

    double time() const;
    static float trackLength();

private:
    struct Impl;
    std::unique_ptr<Impl> d_;
    std::thread thread_;
    std::atomic<bool> running_{false};
};

} // namespace f1sim
