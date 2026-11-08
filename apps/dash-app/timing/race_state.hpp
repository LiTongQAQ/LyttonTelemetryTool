// Race state built from the telemetry stream: standings, per-car sector history and the player's inputs.
//
// feed() runs on the hub's receive thread, snapshot() on the render thread; a mutex sits between them and the
// render thread works on its own copy. Completed laps come from the SessionHistory packets (the game sends one car
// per packet and cycles through the grid), the running lap from LapData.
#pragma once

#include <f1tel/packets.hpp>

#include <array>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

namespace timing {

constexpr uint32_t NO_TIME = 0;

struct LapRecord {
    uint32_t lapMs = 0;
    std::array<uint32_t, 3> sectorMs{}; // 0 = not set
    bool lapValid = true;
    std::array<bool, 3> sectorValid{true, true, true};
};

struct Car {
    bool active = false;
    std::string name;
    uint8_t teamId = 255;
    uint8_t raceNumber = 0;
    uint8_t driverId = 255; // the game's driver id (255 = network human)
    bool ai = false;
    bool you = false; // the player of this machine (header playerCarIndex)
    bool telemetryPublic = true;

    // LapData
    uint8_t position = 0;
    uint8_t lapNum = 0;
    uint8_t sector = 0;              // 0..2, sector being driven
    uint32_t currentLapMs = 0;
    std::array<uint32_t, 2> currentSectorMs{}; // S1, S2 of the running lap once completed
    bool currentLapInvalid = false;
    uint32_t lastLapMs = 0;
    uint32_t gapToCarAheadMs = 0;
    uint32_t gapToLeaderMs = 0;
    uint8_t pitStatus = 0;    // 0 none, 1 pitting, 2 in pit area
    uint8_t resultStatus = 0; // 2 active, 3 finished, 4 DNF, ...
    float lapDistance = 0;    // metres into the lap
    float worldX = 0, worldZ = 0; // Motion: position on the ground plane
    float fwdX = 0, fwdZ = 1;     // Motion: unit vector the car points along
    bool hasPosition = false;
    uint8_t numPitStops = 0;

    // CarStatus
    uint8_t visualTyre = 0;
    uint8_t tyreAgeLaps = 0;

    // SessionHistory
    std::vector<LapRecord> laps; // completed laps, index 0 = lap 1
    uint32_t bestLapMs = 0;
    std::array<uint32_t, 3> bestSectorMs{};
};

struct Inputs {
    float throttle = 0, brake = 0, steer = 0;
    uint16_t speed = 0;
    int8_t gear = 0;
    uint16_t rpm = 0;
    uint8_t clutch = 0; // 0..100
    uint8_t revLights = 0; // the game's shift-light percentage, 0..100
    uint8_t drs = 0;
    uint16_t engineTemp = 0;
    // wheels in the game's order: rear-left, rear-right, front-left, front-right
    std::array<uint8_t, 4> tyreSurface{}, tyreInner{};
    std::array<uint16_t, 4> brakeTemp{};
    std::array<float, 4> tyrePressure{}, tyreWear{};
    // CarStatus
    float fuelKg = 0, fuelLaps = 0, ersJ = 0;
    uint8_t ersMode = 0, drsAllowed = 0;
    std::array<uint8_t, 4> surface{}; // f1tel::Surface under each wheel
    // LapData: the game's own track-limit judgement
    uint8_t cornerCuttingWarnings = 0, totalWarnings = 0, penaltiesS = 0;
};

// The player car's condition, from CarDamage. All values are percent.
struct Damage {
    bool valid = false;
    std::array<uint8_t, 4> tyreDamage{}, brakeDamage{}, blisters{}; // wheel order as in Inputs
    uint8_t frontWingL = 0, frontWingR = 0, rearWing = 0, floor = 0, diffuser = 0, sidepod = 0;
    uint8_t gearbox = 0, engine = 0;
    // power unit wear
    uint8_t ice = 0, mguh = 0, mguk = 0, es = 0, ce = 0, tc = 0;
    bool drsFault = false, ersFault = false, engineBlown = false, engineSeized = false;
};

// Something changed in the car's condition: a fault set or cleared, a part damaged or repaired.
struct StatusEvent {
    float t = 0;   // session time, s
    int lap = 0;
    int level = 0; // 0 recovered / repaired, 1 damage, 2 fault
    std::string text;
};

// Fuel use worked out from fuelInTank over time (the game sends no flow rate).
struct Fuel {
    float kgPerS = 0;      // instantaneous, smoothed over ~1 s
    float kgPer100km = 0;  // instantaneous, 0 when nearly stopped
    float lapUsedKg = 0;   // this lap so far
    float lastLapKg = 0;   // the previous complete lap
    float avgLapKg = 0;    // mean over complete laps
    int lapsCounted = 0;
};

// Per-wheel forces and slip as the game reports them (MotionEx, player car only). Wheel order as in Inputs.
struct WheelForces {
    bool valid = false;
    std::array<float, 4> load{};       // vertical force, N
    std::array<float, 4> slipRatio{};  // longitudinal slip, -1..1 (negative = locking, positive = wheelspin)
    std::array<float, 4> slipAngle{};  // radians
    std::array<float, 4> latForce{}, longForce{}; // N
};

// From the Session packet.
struct Forecast {
    uint8_t minutes = 0; // from now
    uint8_t weather = 0; // 0 clear, 1 light cloud, 2 overcast, 3 light rain, 4 heavy rain, 5 storm
    uint8_t rain = 0;    // chance of rain, percent
    int8_t trackTemp = 0, airTemp = 0;
};

// Assists the session runs with (Session packet; 0 = off unless noted).
struct Assists {
    uint8_t steering = 0, braking = 0;   // braking: 0 off, 1 low, 2 medium, 3 high
    uint8_t gearbox = 0;                 // 1 manual, 2 manual + suggested gear, 3 auto
    uint8_t pit = 0, pitRelease = 0, ers = 0, drs = 0;
    uint8_t racingLine = 0;              // 0 off, 1 corners only, 2 full
    uint8_t racingLineType = 0;          // 0 2D, 1 3D
};

struct SessionInfo {
    bool valid = false;
    int trackId = -1;
    uint16_t trackLength = 0; // m
    uint8_t totalLaps = 0, sessionType = 0, weather = 0, safetyCar = 0;
    int8_t trackTemp = 0, airTemp = 0;
    float sector2Start = 0, sector3Start = 0; // lap distance, m
    uint16_t timeLeft = 0, duration = 0; // s
    float timeLeftAt = 0;                // game session time when timeLeft last changed
    float clock = 0;                     // latest game session time (any packet; stands still while paused)
    float remaining() const              // timeLeft running smoothly between Session packets
    {
        const float r = float(timeLeft) - (clock - timeLeftAt);
        return r < float(timeLeft) - 3.f ? float(timeLeft) - 3.f : r > float(timeLeft) ? float(timeLeft) : r;
    }
    uint8_t pitSpeedLimit = 0;           // km/h
    uint8_t pitIdealLap = 0, pitLatestLap = 0, pitRejoinPosition = 0; // 0 = none
    uint8_t forecastAccuracy = 0;        // 0 perfect, 1 approximate
    std::vector<Forecast> forecast;      // this session's samples, nearest first
    Assists assists;
};

// The player car's body: G forces and attitude (Motion) and the chassis values of MotionEx.
struct Chassis {
    bool valid = false;   // Motion seen
    float gLat = 0, gLong = 0, gVert = 0;
    float pitch = 0, roll = 0; // radians, world attitude
    bool exValid = false; // MotionEx seen
    float cogHeight = 0;  // centre of gravity above the ground, m
    float frontAeroHeight = 0, rearAeroHeight = 0; // m
    float frontRoll = 0, rearRoll = 0, chassisPitch = 0; // radians
    float frontWheelsAngle = 0; // radians
    std::array<float, 3> localVelocity{}, angularVelocity{}; // m/s, rad/s (x right, y up, z forward)
    std::array<float, 4> suspensionPos{}, suspensionVel{}, wheelSpeed{}, camber{}; // wheel order as in Inputs
};

// The player car's running configuration (CarStatus).
struct CarConfig {
    bool valid = false;
    uint8_t tractionControl = 0; // 0 off, 1 medium, 2 full
    uint8_t abs = 0;
    uint8_t fuelMix = 0;         // 0 lean, 1 standard, 2 rich, 3 max
    uint8_t brakeBias = 0;       // percent front
    uint8_t pitLimiter = 0;
    float fuelCapacity = 0;      // kg
    uint16_t maxRPM = 0, idleRPM = 0;
    uint8_t maxGears = 0;
    uint8_t actualTyre = 0, visualTyre = 0, tyreAge = 0;
    int8_t fiaFlag = -1;         // -1 unknown, 0 none, 1 green, 2 blue, 3 yellow
    float iceKW = 0, mgukKW = 0;
};

// The player car's setup (CarSetups).
struct Setup {
    bool valid = false;
    uint8_t frontWing = 0, rearWing = 0, onThrottle = 0, offThrottle = 0;
    float frontCamber = 0, rearCamber = 0, frontToe = 0, rearToe = 0;
    uint8_t frontSuspension = 0, rearSuspension = 0, frontAntiRollBar = 0, rearAntiRollBar = 0;
    uint8_t frontRideHeight = 0, rearRideHeight = 0;
    uint8_t brakePressure = 0, brakeBias = 0, engineBraking = 0;
    std::array<float, 4> tyrePressure{}; // wheel order as in Inputs
    uint8_t ballast = 0;
    float fuelLoad = 0;
    float nextFrontWing = 0;
};

struct Snapshot {
    uint64_t sessionUID = 0;
    bool hasData = false;
    uint8_t playerIdx = 255;
    uint8_t numActive = 0;
    std::array<Car, f1tel::MAX_CARS> cars{};
    Inputs player;
    Damage damage;
    Fuel fuel;
    WheelForces wheels;
    SessionInfo session;
    Chassis chassis;
    CarConfig config;
    Setup setup;
    std::vector<StatusEvent> events; // newest last, at most 40
    std::array<uint32_t, 3> sessionBestSectorMs{}; // over all cars, valid sectors only
    uint32_t sessionBestLapMs = 0;

    // Car indices sorted by position (only cars with a position).
    std::vector<int> order() const;
    // The cars the compact tower shows, in position order: leader, two ahead of you, you, two behind (6 rows).
    // Leading: you + the next five. Last: the five ahead + you. P2-P4: the top six. Second to last: leader + the
    // last five. Always `rows` cars (or all of them when fewer are classified).
    std::vector<int> compactOrder(int rows = 6) const;
};

// Gap of `car` relative to the player, from the gaps to the leader: negative = ahead of you, positive = behind.
// `laps` is set when the cars are on different laps relative to the leader (lapped traffic): -1 = a lap ahead.
struct RelativeGap {
    int32_t ms = 0;
    int laps = 0;
    bool valid = false; // false when the player isn't classified yet
};
int lapsDown(const Snapshot &s, int car); // whole laps behind the leader
RelativeGap relativeToPlayer(const Snapshot &s, int car);

// Practice, qualifying, sprint shootout and time trial are ranked on the best lap, so their gaps are best-lap
// differences, not distances on track.
bool rankedOnBestLap(const Snapshot &s);
// Best lap of `car` minus best lap of `ref` (ms; negative = faster). Invalid when either has no lap time yet.
struct LapGap {
    int32_t ms = 0;
    bool valid = false;
};
LapGap bestLapGap(const Snapshot &s, int car, int ref);
// The car one place ahead of `car`, or -1.
int carAhead(const Snapshot &s, int car);

// Three letters for the tower: the surname ("NOR") when the name is plain ASCII; when it is not (the game in Chinese
// sends Chinese names, which the panel font cannot draw) the code for the game's driver id, else "#<race number>".
std::string driverCode(const Car &c);
// Your own code (the game shows it in replays, the telemetry has only the name): used for the player's car when set.
void setMyCode(const std::string &code);
std::string playerCode(const std::string &name); // first Latin word: first letter + consonants ("Lyton·Yao" -> LTN), "PLAYER" if none

// How a sector time compares, for the purple / green / yellow colours.
enum class Mark { None, SessionBest, PersonalBest, Slower, Invalid };
Mark markSector(const Snapshot &s, int car, int sector, uint32_t ms, bool valid);
Mark markLap(const Snapshot &s, int car, uint32_t ms, bool valid);

class RaceState {
public:
    void feed(const uint8_t *data, size_t len);
    Snapshot snapshot() const;
    void reset();

private:
    void recomputeBests();
    void damageEvents(const Damage &prev, const Damage &now, float t);
    void resetFuel();
    mutable std::mutex m_;
    Snapshot s_;
    std::deque<std::pair<float, float>> fuelHist_; // (session time, kg)
    float lapStartFuel_ = -1, fuelSum_ = 0;
    int fuelLap_ = 0;
};

// Session types (Session packet, F1 25 specification): 1-4 practice, 5-9 qualifying, 10-14 sprint shootout,
// 15 race, 16 "race 2" (the sprint on a sprint weekend), 17 race 3, 18 time trial.
enum class SessionGroup { Unknown, Practice, Qualifying, SprintShootout, Race, Sprint, TimeTrial };
SessionGroup sessionGroup(int type);
const char *sessionTypeName(int type);  // "练习赛 1", "排位赛 Q2", "正赛", ...
const char *sessionGroupName(SessionGroup g);
const char *sessionTypeTag(int type);   // file name tag: "FP1", "Q2", "SQ1", "Race", "Sprint", "TT"

// "1:31.482", "31.482", "+1.208"
std::string formatLap(uint32_t ms);
std::string formatSector(uint32_t ms);
std::string formatGap(uint32_t ms);
// "-1.208", "+0.000", "+0.842"
std::string formatSignedGap(int32_t ms);
// Three-letter tag from a name: "HAMILTON" -> "HAM"
std::string shortName(const std::string &name);

} // namespace timing
