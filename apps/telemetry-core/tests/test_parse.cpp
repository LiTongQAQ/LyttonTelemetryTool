// Parses real F1 25 captures (tests/data, from the f1-game-library-models-25 crate, MIT) and checks the values.
#include <f1tel/parse.hpp>

#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

static int failures = 0;
#define CHECK(cond)                                                                                                    \
    do {                                                                                                               \
        if (!(cond)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); failures++; }                       \
    } while (0)

static std::vector<char> load(const std::string &name)
{
    std::ifstream f(std::string(F1TEL_TEST_DATA) + "/" + name, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), {}};
}

int main()
{
    {
        auto b = load("Session.bin");
        auto p = f1tel::parse<f1tel::PacketSession>(b.data(), b.size());
        CHECK(p.has_value());
        if (p) {
            std::printf("session: track %d, %u m, %u laps, type %u, S2 %.0f m, S3 %.0f m, air %d C, track %d C\n",
                        p->trackId, p->trackLength, p->totalLaps, p->sessionType, p->sector2LapDistanceStart,
                        p->sector3LapDistanceStart, p->airTemperature, p->trackTemperature);
            CHECK(p->trackId >= -1 && p->trackId < 45);
            CHECK(p->trackLength > 3000 && p->trackLength < 8000);
            CHECK(p->numMarshalZones <= f1tel::MAX_MARSHAL_ZONES);
            CHECK(p->numWeatherForecastSamples <= f1tel::MAX_WEATHER_SAMPLES);
            CHECK(p->sector2LapDistanceStart > 0 && p->sector2LapDistanceStart < p->sector3LapDistanceStart);
            CHECK(p->sector3LapDistanceStart < p->trackLength);
            CHECK(p->airTemperature > -10 && p->airTemperature < 50);
        }
    }
    using namespace f1tel;

    {
        auto b = load("CarTelemetry.bin");
        auto h = read_header(b.data(), b.size());
        CHECK(h);
        CHECK(h.header.gameYear == 25);
        CHECK(h.header.packetId == uint8_t(PacketId::CarTelemetry));
        CHECK(h.header.playerCarIndex == 19);
        CHECK(h.header.secondaryPlayerCarIndex == 255);

        auto p = parse<PacketCarTelemetry>(b.data(), b.size());
        CHECK(p.has_value());
        if (p) {
            const auto &c = p->car[p->header.playerCarIndex];
            std::printf("telemetry: %u km/h gear %d rpm %u throttle %.2f tyreFL %u°C\n", c.speed, c.gear, c.engineRPM,
                        c.throttle, c.tyresSurfaceTemperature.fl);
            CHECK(c.throttle >= 0.f && c.throttle <= 1.f);
            CHECK(c.brake >= 0.f && c.brake <= 1.f);
            CHECK(c.gear >= -1 && c.gear <= 8);
            CHECK(c.engineRPM < 20000);
        }
        CHECK(!parse<PacketCarStatus>(b.data(), b.size())); // wrong type is rejected
    }
    {
        auto b = load("CarStatus.bin");
        auto p = parse<PacketCarStatus>(b.data(), b.size());
        CHECK(p.has_value());
        if (p) {
            const auto &c = p->car[p->header.playerCarIndex];
            std::printf("status: bias %u mix %u fuel %.1f/%.1f ers %.0f J\n", c.frontBrakeBias, c.fuelMix, c.fuelInTank,
                        c.fuelCapacity, c.ersStoreEnergy);
            CHECK(c.frontBrakeBias >= 50 && c.frontBrakeBias <= 70);
            CHECK(c.fuelMix <= 3);
            CHECK(c.fuelInTank <= c.fuelCapacity);
            CHECK(c.ersStoreEnergy >= 0.f && c.ersStoreEnergy <= 4.0e6f);
        }
    }
    {
        auto b = load("LapData.bin");
        auto p = parse<PacketLapData>(b.data(), b.size());
        CHECK(p.has_value());
        if (p) {
            const auto &c = p->car[p->header.playerCarIndex];
            std::printf("lap: P%u lap %u sector %u current %u ms\n", c.carPosition, c.currentLapNum, c.sector,
                        c.currentLapTimeInMS);
            CHECK(c.carPosition >= 1 && c.carPosition <= MAX_CARS);
            CHECK(c.sector <= 2);
        }
    }
    {
        auto b = load("CarDamage.bin");
        auto p = parse<PacketCarDamage>(b.data(), b.size());
        CHECK(p.has_value());
        if (p) {
            const auto &c = p->car[p->header.playerCarIndex];
            std::printf("damage: wear FL %.1f%% FR %.1f%% RL %.1f%% RR %.1f%%\n", c.tyresWear.fl, c.tyresWear.fr,
                        c.tyresWear.rl, c.tyresWear.rr);
            CHECK(c.tyresWear.fl >= 0.f && c.tyresWear.fl <= 100.f);
        }
    }
    {
        auto b = load("Event.bin");
        auto p = parse<PacketEvent>(b.data(), b.size());
        CHECK(p.has_value());
        if (p) CHECK(std::string(p->eventStringCode, 4) == "BUTN");
    }
    {
        auto b = load("Session.bin");
        CHECK(read_header(b.data(), b.size())); // known id and size, layout not decoded yet
        b.pop_back();
        auto h = read_header(b.data(), b.size());
        CHECK(!h && *h.error == ParseError::WrongSize);
    }

    std::printf(failures ? "%d FAILED\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
