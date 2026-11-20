#pragma once

#include <cstddef>
#include <cstdint>

#include "protocol/Cia402.hpp"
#include "protocol/SdoClient.hpp"

/**
 * Which operation mode the console runs the drive in.
 */
enum class MotionMode : uint8_t {
    NONE,     // Not running
    VELOCITY, // Profile velocity (Velocity screen)
    POSITION, // Profile position (Position screen): moving to a target or holding it
    TORQUE,   // Profile torque (Torque screen)
};

// A motion sample value that wasn't read or doesn't apply; plots leave a gap
constexpr int32_t SAMPLE_NONE = INT32_MAX;

/**
 * One motion sample for plots, taken about every MotorService::SAMPLE_INTERVAL_MS.
 */
struct MotionSample {
    int32_t velocityRpm;              // 606C
    int32_t targetRpm;                // Target velocity the drive was given, 0 while not running
    int32_t positionMilliturns;       // 6064 from the console zero, SAMPLE_NONE if not read
    int32_t targetPositionMilliturns; // Set-point the drive is moving to or holding, SAMPLE_NONE outside position mode
    int32_t currentCentiamps;         // 6078 converted with the rated current, SAMPLE_NONE if not read
    int32_t currentPerMille;          // 6078 as read, 0.1 % of rated (a magnitude in every mode), SAMPLE_NONE if not read
    int32_t targetTorquePerMille;     // Torque target the drive was given, SAMPLE_NONE outside torque mode
};

/**
 * How serious the last message is, for its colour and icon on screen.
 */
enum class MessageLevel : uint8_t {
    INFO,    // Progress and results: found, running, settled, restored
    WARNING, // Something to check: still faulted, refused for a reason, drive lost, an alarm reported
    ERROR,   // A failure: an aborted write, a timeout, a refused save
};

/**
 * One line of the console's session log (D-024): what happened, when, and what the motor was doing.
 */
struct SessionLogEntry {
    enum class Kind : uint8_t {
        ALARM,           // EMCY with an alarm code
        CLEARED,         // EMCY 0x0000: the drive cleared its alarms
        RESET,           // The console reset a fault; resetBy says what did
        CONNECTED,       // A drive was found; serialNumber says which
        LOST,            // The drive stopped answering
        POWERED_OFF,     // The drive stopped answering right after undervoltage
        RESTORED,        // Restore defaults done; the drive reloaded its factory settings
        RESTORE_REFUSED, // The drive answered the restore with a save fault: nothing changed
        SAVED,           // Parameters saved to the drive's flash (PAR-005)
        SAVE_REFUSED,    // The drive answered the save with a save fault: nothing saved
        RECOVERED,       // Blank-flash recovery wrote the table's values (D-031); writeCount and refusedCount say how many
        RECOVER_FAILED,  // The recovery stopped before the restore; resetBy names the step
    };

    Kind kind = Kind::ALARM;
    uint32_t timeMs = 0;                // Console uptime
    uint16_t code = 0;                  // Alarm code (ALARM, RESET)
    uint8_t errorRegister = 0;          // 1001 bits from the EMCY (ALARM)
    uint16_t count = 1;                 // Identical entries within a second, merged
    const char* resetBy = "";           // "START", "GO" or "RESET FAULT" (RESET); a string literal
    uint32_t serialNumber = 0;          // CONNECTED
    MotionMode mode = MotionMode::NONE; // Running mode at the time, NONE when stopped
    int32_t velocityRpm = 0;            // Last motion sample at the time
    int32_t currentCentiamps = 0;
    int32_t positionMilliturns = 0;
    bool hasMotion = false;    // The motion fields hold a sample
    uint16_t writeCount = 0;   // RECOVERED: objects written
    uint16_t refusedCount = 0; // RECOVERED: writes the drive refused
};

/**
 * Settings that can stop a motor from running without any fault, read at connect before the console writes anything
 * (FLT-005, D9 of the Faults plan).
 */
struct RunCheckValues {
    static constexpr size_t INPUT_COUNT = 3; // DI1-DI3; DX0 and the virtual inputs aren't checked

    bool isRead = false;
    uint16_t controlMode = 0;                  // 2002:01 as the motor came; 0 = CiA 402
    uint16_t inputFunctions[INPUT_COUNT] = {}; // 2003:03, :05, :07 (table 7-2)
    uint16_t inputStates = 0;                  // 200B:05
    uint8_t homingMethod = 0;                  // 6098; 21 = configured for a PTU
    bool isParameterFlashBlank = false;        // The sentinels 2000:09, 2000:11, 2000:19 and 2003:03 all read all ones: an erased parameter flash (D-031)
};

/**
 * What the drive records about faults, read at connect before any write and again after each EMCY or fault reset
 * (D-024). Alarm words are in the 200E format (protocol/AlarmWord.hpp).
 */
struct FaultRecord {
    static constexpr size_t HISTORY_LENGTH = 16;     // 1003:01-:10
    static constexpr size_t MAX_REACTION_COUNT = 24; // 200E:01 onwards; 17 on firmware V0.2.5.a5

    bool isRead = false;                         // A read was done since connecting
    char readError[64] = "";                     // Why the history couldn't be read, empty when it could
    uint8_t errorRegister = 0;                   // 1001
    uint32_t history[HISTORY_LENGTH] = {};       // 1003:01-:10, newest first; 0 = empty slot
    size_t historyCount = 0;                     // Non-empty entries
    uint32_t reactions[MAX_REACTION_COUNT] = {}; // 200E:01 onwards
    size_t reactionCount = 0;                    // Entries read
    size_t changedReactionCount = 0;             // Entries that differ from the factory values
};

/**
 * One parameter as read from the drive (PAR-002), at the same position as its entry in Parameters::ENTRIES.
 */
struct ParameterValue {
    uint32_t raw = 0;                 // As read; sign-extend by the entry's type
    uint32_t ownRaw = 0;              // The motor's own value, captured at connect before the console wrote anything
    uint32_t abortCode = 0;           // The drive's SDO abort code when result is ABORTED
    SdoResult result = SdoResult::OK; // Outcome of the last read
    bool isRead = false;              // raw or the error is from a read since connecting
    bool hasOwnValue = false;         // ownRaw is known: session and console-owned entries (D-028)
};

/**
 * A parameter written since the last save (PAR-004): run-time only until SAVE, gone at the motor's power-off.
 */
struct UnsavedChange {
    uint16_t position = 0; // Entry position in Parameters::ENTRIES
    uint32_t before = 0;   // The value before the first write since the last save
    uint32_t now = 0;      // The value read back after the last write
};

/**
 * What a save would keep besides the user's changes (D-028), for the confirm and the save itself.
 */
struct SavePreview {
    bool keepsBatteryUndervoltage = false; // The bus is too low to put back the motor's own limit: 2001:10 stays low
    uint32_t batteryUndervoltageV = 0;     // The value saved then
    uint32_t ownUndervoltageV = 0;         // The motor's own limit
    bool lowersRatedVoltage = false;       // The bus is under 80 % of 2000:07: lowered for the flash write, and saved
    uint32_t ratedVoltageV = 0;            // 2000:07 as it is
    uint32_t loweredRatedVoltageV = 0;     // The value saved then
    uint32_t flashMinimumDeciV = 0;        // The bus voltage flash writes need with 2000:07 as it is, 0.1 V
    bool usesFactoryForOwn = false;        // Own values unknown (hasEarlierSessionValues): factory values go back instead
};

/**
 * The drive's offline parameter identification run from the console (Motor calibration plan, D-032): initial angle,
 * resistance, inductances, Kt and inertia, with the values before and after. Published while it runs and when it ends.
 */
struct CalibrationStatus {
    enum class State : uint8_t {
        IDLE,      // No run this session, or the result was discarded
        RUNNING,   // In progress; stage and speedRpm are live
        DONE,      // The drive reached stage 3 and switched off; after[] holds the identified values
        FAILED,    // The drive raised an alarm or didn't finish; before[] was written back
        CANCELLED, // STOP ended it; before[] was written back
    };

    // The constants, in the order of the result table
    enum Constant : uint8_t {
        INITIAL_ANGLE,
        STATOR_RESISTANCE,
        STATOR_INDUCTANCE_LQ,
        STATOR_INDUCTANCE_LD,
        TORQUE_COEFFICIENT,
        ROTOR_INERTIA,
        CONSTANT_COUNT,
    };

    State state = State::IDLE;
    uint8_t stage = 0;                    // 2009:06: 1 locking the shaft, 2 spinning, 3 done
    int32_t speedRpm = 0;                 // 606C while running
    uint32_t before[CONSTANT_COUNT] = {}; // As the drive had them when the run started
    uint32_t after[CONSTANT_COUNT] = {};  // As identified (DONE only)
    uint16_t faultCode = 0;               // 603F when FAILED
    char reason[64] = "";                 // Why it failed or stopped, for the message
    uint32_t elapsedMs = 0;               // Run time so far, or at the end
};

/**
 * Snapshot of the drive, published by MotorService and copied into UiState by the UI bridge.
 *
 * Values are in the drive's own units (named in each field), converted only where they are shown.
 */
struct MotorStatus {
    // Connection
    bool isConnected = false;            // Drive answered the identity read
    bool isRunning = false;              // A start sequence completed and no stop since
    bool isLostWhileRunning = false;     // The drive stopped answering while running; with the heartbeat on it halts itself (D-023)
    bool isBelowPackCutoff = false;      // Bus voltage under the pack cutoff (D-005); motion is refused
    bool isRecoveredNotRestored = false; // RECOVER wrote the table's values and the restore hasn't succeeded yet: SAVE is refused (D-031)
    bool needsPowerCycle = false;        // RECOVER or a calibration rewrote values this session; motion is refused until the motor boots again (runaway on motor 159709, 2026-10-15)

    // Calibration (D-032)
    CalibrationStatus calibration;
    uint32_t calibrationVersion = 0; // Changes on every calibration update; the Calibrate screen fetches the status then

    // Identity, read once on connect
    uint32_t vendorId = 0;         // 1018:01
    uint32_t productCode = 0;      // 1018:02
    uint32_t serialNumber = 0;     // 1018:04
    char deviceName[48] = "";      // 1008
    char hardwareVersion[32] = ""; // 1009
    char softwareVersion[32] = ""; // 100A
    uint32_t nodeId = 0;           // 200C:02, as configured (takes effect after a power cycle)
    uint32_t bitrateCode = 0;      // 200C:03, 8 = 1 Mbit/s

    // Configuration, read once on connect
    uint32_t encoderIncrements = 0;           // 608F:01, counts per motor revolution
    uint32_t ratedCurrentCentiamps = 0;       // 2000:09, 0.01 A
    int32_t ratedTorqueMilliNm = 0;           // 6076 in mN·m (the drive stores 0.01 N·m)
    uint32_t statorResistanceMilliohms = 0;   // 2000:12, 0.001 Ω
    uint32_t ratedSpeedRpm = 0;               // 2000:0E
    uint32_t maxMotorSpeedRpm = 0;            // 6080
    uint32_t watchdogMs = 0;                  // 200C:1B, 0 = off
    uint32_t heartbeatMs = 0;                 // 1017 now; the console turns it on so a lost bus raises 0x7501 (D-023)
    uint32_t originalHeartbeatMs = 0;         // 1017 as the drive came
    uint32_t communicationFaultAction = 0;    // 200E:09, the reaction to 0x7501
    uint32_t quickStopDecelerationCounts = 0; // 6085, counts/s²
    int32_t quickStopOptionCode = 0;          // 605A
    uint32_t maxProfileVelocityCounts = 0;    // 607F, counts/s
    uint32_t undervoltageLimitV = 0;          // 2001:10 now; the drive won't enable below this bus voltage
    uint32_t originalUndervoltageLimitV = 0;  // 2001:10 as the drive came, before the console lowered it

    // State
    uint16_t statusword = 0; // 6041
    DriveState state = DriveState::UNKNOWN;
    uint16_t errorCode = 0; // 603F

    // Targets the UI set, so a screen opened later starts from them instead of changing a running motor
    int32_t targetVelocityRpm = 0;
    int32_t targetAccelerationRpmPerSecond = 0;

    // Limits the targets are clamped to, from the drive (fallbacks until it has been read)
    int32_t velocityLimitRpm = 0;              // Rated speed, capped by 6080 and 607F
    int32_t accelerationLimitRpmPerSecond = 0; // Quick-stop deceleration 6085, so a ramp is never steeper than STOP

    // Velocity samples taken so far; a plot fetches the new ones when this changes
    uint32_t motionSampleCount = 0;

    // Position mode (Position mode plan). User units are what 607A/6064 count in; equal to encoder counts at the
    // default 1:1 gear ratio
    MotionMode motionMode = MotionMode::NONE;  // Meaningful while isRunning
    bool isHalted = false;                     // Position mode stopped with halt (bit 8), holding
    int32_t sentTargetUnits = 0;               // Last set-point the drive acknowledged, user units
    bool isGoWithoutEffect = false;            // GO wouldn't move the shaft: shown as an outline (set by getStatus)
    int32_t positionUnits = 0;                 // 6064
    int32_t positionZeroUnits = 0;             // Console zero in 6064 units (D-021)
    int32_t unitsPerTurn = 0;                  // From 608F and 6091; 0 if unknown
    int32_t positionMilliturns = 0;            // 6064 from the zero, in 0.001 turn
    int32_t positionDecidegrees = 0;           // The same in 0.1 degree
    int32_t targetPositionMilliturns = 0;      // Pending absolute target from the zero; sent on GO
    bool isPositionRelative = false;           // GO moves by moveByMilliturns instead of to the absolute target (D4)
    int32_t moveByMilliturns = 0;              // Pending relative move, 0.001 turn
    int32_t relativeBaseMilliturns = 0;        // Where a relative move starts from: the last target while moving to or holding it, else the position
    int32_t moveSpeedRpm = 0;                  // 6081 for the next GO
    int32_t positionLimitMinUnits = 0;         // 607D:01 now
    int32_t positionLimitMaxUnits = 0;         // 607D:02 now
    int32_t originalPositionLimitMinUnits = 0; // 607D:01 as the drive came, before the console widened it (D-020)
    int32_t originalPositionLimitMaxUnits = 0; // 607D:02 as the drive came

    // Torque mode (Torque mode plan)
    int32_t targetTorquePerMille = 0;         // 6071 to follow, 0.1 % of rated
    int32_t torqueSlopePerMillePerSecond = 0; // 6087, 0.1 % per second
    int32_t torqueSpeedLimitForwardRpm = 0;   // 2007:10, read at connect and when written
    int32_t torqueDeciMilliNm = 0;            // Torque from the measured current, 0.1 mN·m, unsigned like 6078
    int32_t torqueSpeedLimitReverseRpm = 0;   // 2007:11

    // Step test (PAR-010): settings the UI set, and whether one runs
    bool isStepTest = false;          // A step test drives the motor; it ends when the motor stops
    bool isStepTestPosition = false;  // Position steps (relative moves) instead of velocity steps
    int32_t stepTestVelocityARpm = 0; // Velocity steps alternate between A and B
    int32_t stepTestVelocityBRpm = 0;
    int32_t stepTestStepMilliturns = 0;           // Position steps move this far and back, from where the test starts
    int32_t stepTestDwellMs = 0;                  // Time at each target
    int32_t stepTestAccelerationRpmPerSecond = 0; // The test's ramp; 30000 rpm/s until set
    int32_t stepTestMoveSpeedRpm = 0;             // Position steps' speed; the speed limit until set

    // Measurements
    int32_t velocityRpm = 0;      // 606C
    int32_t positionCounts = 0;   // 6063
    int32_t currentPerMille = 0;  // 6078, per mille of the rated current (inferred on the bench, not the manuals' 0.01 A)
    int32_t currentCentiamps = 0; // From 6078 and 2000:09, 0.01 A; 0 while the rated current is unknown
    int32_t busVoltageDeciV = 0;  // 200B:15, 0.1 V
    int32_t temperatureC = 0;     // 200B:16

    // Last emergency message (EMCY, 0x80 + node); the sequence number changes on every new one
    uint16_t emcyErrorCode = 0;
    uint8_t emcyErrorRegister = 0;
    uint32_t emcySequence = 0;

    // Fault record and run checks; the version changes on every new read
    RunCheckValues runChecks; // Read at connect, before the fault record
    FaultRecord faultRecord;
    uint32_t faultRecordVersion = 0;

    // Changes on every session log change (the log itself is fetched with MotorService::getSessionLog)
    uint32_t sessionLogVersion = 0;

    // Parameter read (PAR-002); the values are fetched with MotorService::getParameterValues
    uint32_t parametersVersion = 0; // Changes when a read finishes or the values are cleared
    uint16_t parametersRead = 0;    // Entries read so far in the current or last read
    uint16_t parametersTotal = 0;   // Entries in the current or last read
    bool isReadingParameters = false;
    bool hasEarlierSessionValues = false;
    uint16_t unsavedChangeCount = 0; // Parameters written since the last save (MotorService::getUnsavedChanges) // The motor kept an earlier console session's values; its own are unknown

    // CAN controller
    uint32_t canErrorState = 0;     // twai_error_state_t
    uint8_t busNodeId = 1;          // Node the console talks to: 1 unless a search found the drive elsewhere (D-026)
    uint32_t busBitrate = 1000000;  // Bitrate the console runs at, bit/s
    uint32_t searchRoundCount = 0;  // Full sweeps of every node ID and bitrate without an answer since the last connect
    uint32_t canTransmitErrors = 0; // TEC
    uint32_t canReceiveErrors = 0;  // REC
    uint32_t canReceiveDrops = 0;   // Frames lost to a full receive queue

    // Last event (D8 of the App structure plan): the sequence number changes on every new message, even a repeated one
    char message[128] = "Searching for the drive";
    MessageLevel messageLevel = MessageLevel::INFO; // Colour and icon of the message
    uint32_t messageSequence = 0;
};
