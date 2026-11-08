// F1 25 UDP telemetry packet layouts (packet format 2025).
//
// Field order and sizes follow EA's official F1 25 UDP specification, cross-checked against the MIT-licensed
// f1-game-library-models-25 crate. Every struct is packed and its size is pinned with static_assert against the
// packet sizes the spec gives, so a wrong field breaks the build instead of shifting every value after it.
// All values are little-endian, as the game sends them on Windows.
#pragma once

#include <cstddef>
#include <cstdint>

namespace f1tel {

constexpr uint16_t PACKET_FORMAT_2025 = 2025;
constexpr int MAX_CARS = 22;

enum class PacketId : uint8_t {
    Motion = 0,
    Session = 1,
    LapData = 2,
    Event = 3,
    Participants = 4,
    CarSetups = 5,
    CarTelemetry = 6,
    CarStatus = 7,
    FinalClassification = 8,
    LobbyInfo = 9,
    CarDamage = 10,
    SessionHistory = 11,
    TyreSets = 12,
    MotionEx = 13,
    TimeTrial = 14,
    LapPositions = 15,
    Count = 16,
};

// Full packet size (header included) for each packet id, from the spec.
constexpr size_t PACKET_SIZES[] = {1349, 753, 1285, 45, 1284, 1133, 1352, 1239,
                                   1042, 954, 1041, 1460, 231, 273,  101,  1131};
static_assert(sizeof(PACKET_SIZES) / sizeof(PACKET_SIZES[0]) == size_t(PacketId::Count));

#pragma pack(push, 1)

// Per-wheel values. The game orders wheels rear-left, rear-right, front-left, front-right.
template <typename T> struct Wheels {
    T rl, rr, fl, fr;
};

struct PacketHeader {
    uint16_t packetFormat; // 2025
    uint8_t gameYear;      // 25
    uint8_t gameMajorVersion;
    uint8_t gameMinorVersion;
    uint8_t packetVersion;
    uint8_t packetId; // PacketId
    uint64_t sessionUID;
    float sessionTime;
    uint32_t frameIdentifier;
    uint32_t overallFrameIdentifier;
    uint8_t playerCarIndex;
    uint8_t secondaryPlayerCarIndex; // 255 when there is no second player
};
static_assert(sizeof(PacketHeader) == 29);

// ---- Session (id 1) -------------------------------------------------------------------------------------------
struct MarshalZone {
    float zoneStart; // fraction of the lap, 0..1
    int8_t zoneFlag; // -1 invalid, 0 none, 1 green, 2 blue, 3 yellow
};
static_assert(sizeof(MarshalZone) == 5);

struct WeatherForecastSample {
    uint8_t sessionType;
    uint8_t timeOffset; // minutes
    uint8_t weather;    // 0 clear, 1 light cloud, 2 overcast, 3 light rain, 4 heavy rain, 5 storm
    int8_t trackTemperature;
    int8_t trackTemperatureChange; // 0 up, 1 down, 2 no change
    int8_t airTemperature;
    int8_t airTemperatureChange;
    uint8_t rainPercentage;
};
static_assert(sizeof(WeatherForecastSample) == 8);

constexpr int MAX_MARSHAL_ZONES = 21;
constexpr int MAX_WEATHER_SAMPLES = 64;

struct PacketSession {
    PacketHeader header;
    uint8_t weather;
    int8_t trackTemperature; // °C
    int8_t airTemperature;   // °C
    uint8_t totalLaps;
    uint16_t trackLength; // m
    uint8_t sessionType;
    int8_t trackId; // -1 unknown
    uint8_t formula;
    uint16_t sessionTimeLeft; // s
    uint16_t sessionDuration; // s
    uint8_t pitSpeedLimit;    // km/h
    uint8_t gamePaused;
    uint8_t isSpectating;
    uint8_t spectatorCarIndex;
    uint8_t sliProNativeSupport;
    uint8_t numMarshalZones;
    MarshalZone marshalZones[MAX_MARSHAL_ZONES];
    uint8_t safetyCarStatus; // 0 none, 1 full, 2 virtual, 3 formation lap
    uint8_t networkGame;
    uint8_t numWeatherForecastSamples;
    WeatherForecastSample weatherForecastSamples[MAX_WEATHER_SAMPLES];
    uint8_t forecastAccuracy;
    uint8_t aiDifficulty;
    uint32_t seasonLinkIdentifier;
    uint32_t weekendLinkIdentifier;
    uint32_t sessionLinkIdentifier;
    uint8_t pitStopWindowIdealLap;
    uint8_t pitStopWindowLatestLap;
    uint8_t pitStopRejoinPosition;
    uint8_t steeringAssist;
    uint8_t brakingAssist;
    uint8_t gearboxAssist;
    uint8_t pitAssist;
    uint8_t pitReleaseAssist;
    uint8_t ersAssist;
    uint8_t drsAssist;
    uint8_t dynamicRacingLine;
    uint8_t dynamicRacingLineType;
    uint8_t gameMode;
    uint8_t ruleSet;
    uint32_t timeOfDay; // minutes since midnight
    uint8_t sessionLength;
    uint8_t speedUnitsLeadPlayer;
    uint8_t temperatureUnitsLeadPlayer;
    uint8_t speedUnitsSecondaryPlayer;
    uint8_t temperatureUnitsSecondaryPlayer;
    uint8_t numSafetyCarPeriods;
    uint8_t numVirtualSafetyCarPeriods;
    uint8_t numRedFlagPeriods;
    uint8_t equalCarPerformance;
    uint8_t recoveryMode;
    uint8_t flashbackLimit;
    uint8_t surfaceType;
    uint8_t lowFuelMode;
    uint8_t raceStarts;
    uint8_t tyreTemperature;
    uint8_t pitLaneTyreSim;
    uint8_t carDamage;
    uint8_t carDamageRate;
    uint8_t collisions;
    uint8_t collisionsOffForFirstLapOnly;
    uint8_t mpUnsafePitRelease;
    uint8_t mpOffForGriefing;
    uint8_t cornerCuttingStringency;
    uint8_t parcFermeRules;
    uint8_t pitStopExperience;
    uint8_t safetyCar;
    uint8_t safetyCarExperience;
    uint8_t formationLap;
    uint8_t formationLapExperience;
    uint8_t redFlags;
    uint8_t affectsLicenceLevelSolo;
    uint8_t affectsLicenceLevelMP;
    uint8_t numSessionsInWeekend;
    uint8_t weekendStructure[12];
    float sector2LapDistanceStart; // m
    float sector3LapDistanceStart; // m
};
static_assert(sizeof(PacketSession) == 753);

// Surface under each wheel (CarTelemetryData::surfaceType).
enum class Surface : uint8_t {
    Tarmac = 0, RumbleStrip = 1, Concrete = 2, Rock = 3, Gravel = 4, Mud = 5,
    Sand = 6, Grass = 7, Water = 8, Cobblestone = 9, Metal = 10, Ridged = 11,
};

// ---- Motion (id 0) ----------------------------------------------------------------------------------------------
// World space: x and z span the ground plane, y is up. Direction vectors are normalised and stored as int16
// (divide by 32767).
struct CarMotionData {
    float worldPositionX, worldPositionY, worldPositionZ; // m
    float worldVelocityX, worldVelocityY, worldVelocityZ; // m/s
    int16_t worldForwardDirX, worldForwardDirY, worldForwardDirZ;
    int16_t worldRightDirX, worldRightDirY, worldRightDirZ;
    float gForceLateral, gForceLongitudinal, gForceVertical;
    float yaw, pitch, roll; // radians
};
static_assert(sizeof(CarMotionData) == 60);

struct PacketMotion {
    PacketHeader header;
    CarMotionData car[MAX_CARS];
};
static_assert(sizeof(PacketMotion) == 1349);

// ---- Motion ex (id 13) — the player car only ------------------------------------------------------------------
struct PacketMotionEx {
    PacketHeader header;
    Wheels<float> suspensionPosition;
    Wheels<float> suspensionVelocity;
    Wheels<float> suspensionAcceleration;
    Wheels<float> wheelSpeed;
    Wheels<float> wheelSlipRatio;
    Wheels<float> wheelSlipAngle;
    Wheels<float> wheelLatForce;  // N
    Wheels<float> wheelLongForce; // N
    float heightOfCOGAboveGround;
    float localVelocityX, localVelocityY, localVelocityZ;
    float angularVelocityX, angularVelocityY, angularVelocityZ;
    float angularAccelerationX, angularAccelerationY, angularAccelerationZ;
    float frontWheelsAngle; // radians
    Wheels<float> wheelVertForce; // N
    float frontAeroHeight, rearAeroHeight;
    float frontRollAngle, rearRollAngle;
    float chassisYaw;
    float chassisPitch;
    Wheels<float> wheelCamber;
    Wheels<float> wheelCamberGain;
};
static_assert(sizeof(PacketMotionEx) == 273);

// ---- Car telemetry (id 6) ------------------------------------------------------------------------------------
struct CarTelemetryData {
    uint16_t speed; // km/h
    float throttle; // 0..1
    float steer;    // -1 (full left) .. 1 (full right)
    float brake;    // 0..1
    uint8_t clutch; // 0..100
    int8_t gear;    // -1 reverse, 0 neutral, 1..8
    uint16_t engineRPM;
    uint8_t drs; // 0 off, 1 on
    uint8_t revLightsPercent;
    uint16_t revLightsBitValue; // bit 0 = leftmost LED
    Wheels<uint16_t> brakesTemperature;      // °C
    Wheels<uint8_t> tyresSurfaceTemperature; // °C
    Wheels<uint8_t> tyresInnerTemperature;   // °C
    uint16_t engineTemperature;              // °C
    Wheels<float> tyresPressure;             // PSI
    Wheels<uint8_t> surfaceType;
};
static_assert(sizeof(CarTelemetryData) == 60);

struct PacketCarTelemetry {
    PacketHeader header;
    CarTelemetryData car[MAX_CARS];
    uint8_t mfdPanelIndex; // 255 closed, 0 car setup, 1 pits, 2 damage, 3 engine, 4 temperatures
    uint8_t mfdPanelIndexSecondaryPlayer;
    int8_t suggestedGear; // 0 when there is no suggestion
};
static_assert(sizeof(PacketCarTelemetry) == 1352);

// ---- Car status (id 7) ---------------------------------------------------------------------------------------
struct CarStatusData {
    uint8_t tractionControl; // 0 off, 1 medium, 2 full
    uint8_t antiLockBrakes;  // 0 off, 1 on
    uint8_t fuelMix;         // 0 lean, 1 standard, 2 rich, 3 max
    uint8_t frontBrakeBias;  // percent
    uint8_t pitLimiterStatus;
    float fuelInTank;
    float fuelCapacity;
    float fuelRemainingLaps; // value shown on the MFD
    uint16_t maxRPM;
    uint16_t idleRPM;
    uint8_t maxGears;
    uint8_t drsAllowed; // 0 not allowed, 1 allowed
    uint16_t drsActivationDistance; // metres until DRS is available, 0 = not available
    uint8_t actualTyreCompound;
    uint8_t visualTyreCompound;
    uint8_t tyresAgeLaps;
    int8_t vehicleFiaFlags; // -1 unknown, 0 none, 1 green, 2 blue, 3 yellow
    float enginePowerICE;   // W
    float enginePowerMGUK;  // W
    float ersStoreEnergy;   // J
    uint8_t ersDeployMode;  // 0 none, 1 medium, 2 hotlap, 3 overtake
    float ersHarvestedThisLapMGUK;
    float ersHarvestedThisLapMGUH;
    float ersDeployedThisLap;
    uint8_t networkPaused;
};
static_assert(sizeof(CarStatusData) == 55);

// ---- Car setups (id 5) ------------------------------------------------------------------------------------------
// Other players' setups are zeros online.
struct CarSetupData {
    uint8_t frontWing;
    uint8_t rearWing;
    uint8_t onThrottle;  // differential on throttle, percent
    uint8_t offThrottle; // differential off throttle, percent
    float frontCamber;   // degrees
    float rearCamber;
    float frontToe;      // degrees
    float rearToe;
    uint8_t frontSuspension;
    uint8_t rearSuspension;
    uint8_t frontAntiRollBar;
    uint8_t rearAntiRollBar;
    uint8_t frontSuspensionHeight;
    uint8_t rearSuspensionHeight;
    uint8_t brakePressure; // percent
    uint8_t brakeBias;     // percent
    uint8_t engineBraking; // percent
    Wheels<float> tyrePressure; // PSI
    uint8_t ballast;
    float fuelLoad; // kg
};
static_assert(sizeof(CarSetupData) == 50);

struct PacketCarSetups {
    PacketHeader header;
    CarSetupData car[MAX_CARS];
    float nextFrontWingValue; // front wing after the next pit stop (player only)
};
static_assert(sizeof(PacketCarSetups) == 1133);

struct PacketCarStatus {
    PacketHeader header;
    CarStatusData car[MAX_CARS];
};
static_assert(sizeof(PacketCarStatus) == 1239);

// ---- Lap data (id 2) -----------------------------------------------------------------------------------------
struct LapData {
    uint32_t lastLapTimeInMS;
    uint32_t currentLapTimeInMS;
    uint16_t sector1TimeMSPart;
    uint8_t sector1TimeMinutesPart;
    uint16_t sector2TimeMSPart;
    uint8_t sector2TimeMinutesPart;
    uint16_t deltaToCarInFrontMSPart;
    uint8_t deltaToCarInFrontMinutesPart;
    uint16_t deltaToRaceLeaderMSPart;
    uint8_t deltaToRaceLeaderMinutesPart;
    float lapDistance;   // metres, can be negative before the line
    float totalDistance; // metres
    float safetyCarDelta;
    uint8_t carPosition;
    uint8_t currentLapNum;
    uint8_t pitStatus; // 0 none, 1 pitting, 2 in pit area
    uint8_t numPitStops;
    uint8_t sector; // 0, 1, 2
    uint8_t currentLapInvalid;
    uint8_t penalties; // seconds
    uint8_t totalWarnings;
    uint8_t cornerCuttingWarnings;
    uint8_t numUnservedDriveThroughPens;
    uint8_t numUnservedStopGoPens;
    uint8_t gridPosition;
    uint8_t driverStatus; // 0 garage, 1 flying lap, 2 in lap, 3 out lap, 4 on track
    uint8_t resultStatus;
    uint8_t pitLaneTimerActive;
    uint16_t pitLaneTimeInLaneInMS;
    uint16_t pitStopTimerInMS;
    uint8_t pitStopShouldServePen;
    float speedTrapFastestSpeed;
    uint8_t speedTrapFastestLap; // 255 = not set
};
static_assert(sizeof(LapData) == 57);

struct PacketLapData {
    PacketHeader header;
    LapData car[MAX_CARS];
    uint8_t timeTrialPBCarIdx;
    uint8_t timeTrialRivalCarIdx;
};
static_assert(sizeof(PacketLapData) == 1285);

// ---- Car damage (id 10) --------------------------------------------------------------------------------------
struct CarDamageData {
    Wheels<float> tyresWear; // percent
    Wheels<uint8_t> tyresDamage;
    Wheels<uint8_t> brakesDamage;
    Wheels<uint8_t> tyreBlisters;
    uint8_t frontLeftWingDamage;
    uint8_t frontRightWingDamage;
    uint8_t rearWingDamage;
    uint8_t floorDamage;
    uint8_t diffuserDamage;
    uint8_t sidepodDamage;
    uint8_t drsFault;
    uint8_t ersFault;
    uint8_t gearBoxDamage;
    uint8_t engineDamage;
    uint8_t engineMGUHWear;
    uint8_t engineESWear;
    uint8_t engineCEWear;
    uint8_t engineICEWear;
    uint8_t engineMGUKWear;
    uint8_t engineTCWear;
    uint8_t engineBlown;
    uint8_t engineSeized;
};
static_assert(sizeof(CarDamageData) == 46);

struct PacketCarDamage {
    PacketHeader header;
    CarDamageData car[MAX_CARS];
};
static_assert(sizeof(PacketCarDamage) == 1041);

// ---- Participants (id 4) -------------------------------------------------------------------------------------
struct LiveryColour {
    uint8_t red, green, blue;
};

struct ParticipantData {
    uint8_t aiControlled;
    uint8_t driverId; // 255 for network humans
    uint8_t networkId;
    uint8_t teamId;
    uint8_t myTeam;
    uint8_t raceNumber;
    uint8_t nationality;
    char name[32]; // UTF-8, NUL-terminated; "Player" style placeholder when the owner hides online names
    uint8_t yourTelemetry; // 0 restricted, 1 public
    uint8_t showOnlineNames;
    uint16_t techLevel;
    uint8_t platform;
    uint8_t numColours;
    LiveryColour liveryColours[4];
};
static_assert(sizeof(ParticipantData) == 57);

struct PacketParticipants {
    PacketHeader header;
    uint8_t numActiveCars;
    ParticipantData car[MAX_CARS];
};
static_assert(sizeof(PacketParticipants) == 1284);

// ---- Session history (id 11) — one car per packet, the game cycles through the cars --------------------------
constexpr int MAX_LAPS_IN_HISTORY = 100;
constexpr int MAX_TYRE_STINTS = 8;

struct LapHistoryData {
    uint32_t lapTimeInMS;
    uint16_t sector1TimeMSPart;
    uint8_t sector1TimeMinutesPart;
    uint16_t sector2TimeMSPart;
    uint8_t sector2TimeMinutesPart;
    uint16_t sector3TimeMSPart;
    uint8_t sector3TimeMinutesPart;
    uint8_t lapValidBitFlags; // bit 0 lap, bit 1 S1, bit 2 S2, bit 3 S3 valid
};
static_assert(sizeof(LapHistoryData) == 14);

struct TyreStintHistoryData {
    uint8_t endLap; // 255 = current stint
    uint8_t tyreActualCompound;
    uint8_t tyreVisualCompound;
};
static_assert(sizeof(TyreStintHistoryData) == 3);

struct PacketSessionHistory {
    PacketHeader header;
    uint8_t carIdx;
    uint8_t numLaps;
    uint8_t numTyreStints;
    uint8_t bestLapTimeLapNum; // 1-based, 0 = none
    uint8_t bestSector1LapNum;
    uint8_t bestSector2LapNum;
    uint8_t bestSector3LapNum;
    LapHistoryData lap[MAX_LAPS_IN_HISTORY];
    TyreStintHistoryData stint[MAX_TYRE_STINTS];
};
static_assert(sizeof(PacketSessionHistory) == 1460);

// ---- Event (id 3)--------------------------------------------------------------------------------------------
// The 16-byte details union is kept raw; decode it by eventStringCode ("SCAR", "BUTN", "PENA", ...).
struct PacketEvent {
    PacketHeader header;
    char eventStringCode[4];
    uint8_t details[12];
};
static_assert(sizeof(PacketEvent) == 45);

#pragma pack(pop)

} // namespace f1tel
