#pragma once

#include "lvgl.h"
#include "services/LatencyStatus.hpp"
#include "services/MotorStatus.hpp"
#include "services/SystemStatus.hpp"

/**
 * State owned outside the UI, as LVGL subjects that widgets bind to (D-015).
 *
 * Integer subjects hold the drive's own units (named like the MotorStatus fields), formatted where shown; strings only
 * for text. Unsigned values (serial number, ids) are stored bit for bit; read them back with a uint32_t cast. update() copies the service snapshots in; LVGL notifies bound widgets only when a value
 * changes. Only the LVGL task touches this: update() from the bridge timer, reads from widgets and observers.
 */
class UiState {
  public:
    /**
     * The drive, from MotorStatus.
     */
    struct Motor {
        lv_subject_t isConnected;
        lv_subject_t isRunning;
        lv_subject_t isLostWhileRunning; // Drive stopped answering while running (D7)
        lv_subject_t isBelowPackCutoff;  // Motion refused below the pack cutoff (D-005)
        lv_subject_t state;              // DriveState as int
        lv_subject_t statusword;
        lv_subject_t errorCode;
        lv_subject_t targetVelocityRpm;               // What the UI last set, not what the drive does
        lv_subject_t targetAccelerationRpmPerSecond;  // What the UI last set
        lv_subject_t velocityLimitRpm;                // Targets are clamped to ± this (rated speed)
        lv_subject_t accelerationLimitRpmPerSecond;   // Acceleration is clamped to this (quick-stop deceleration)
        lv_subject_t ratedSpeedRpm;                   // 2000:0E
        lv_subject_t maxMotorSpeedRpm;                // 6080
        lv_subject_t motionMode;                      // MotionMode as int; meaningful while isRunning
        lv_subject_t isHalted;                        // Position mode halted and holding
        lv_subject_t isGoWithoutEffect;               // GO wouldn't move the shaft: shown as an outline
        lv_subject_t positionMilliturns;              // 6064 from the console zero, 0.001 turn
        lv_subject_t positionDecidegrees;             // The same in 0.1 degree
        lv_subject_t targetPositionMilliturns;        // Pending absolute position target, 0.001 turn
        lv_subject_t isPositionRelative;              // 0 absolute, 1 relative (D4 of the Position mode plan)
        lv_subject_t moveByMilliturns;                // Pending relative move, 0.001 turn
        lv_subject_t relativeBaseMilliturns;          // Where a relative move starts, 0.001 turn
        lv_subject_t moveSpeedRpm;                    // 6081 for the next GO
        lv_subject_t targetTorquePerMille;            // Torque target, 0.1 % of rated current
        lv_subject_t ratedTorqueMilliNm;              // 6076 in mN·m
        lv_subject_t targetTorqueDeciMilliNm;         // The torque target in 0.1 mN·m, from the per mille and 6076
        lv_subject_t maxTorqueDeciMilliNm;            // The target's limit in 0.1 mN·m (100 % of rated)
        lv_subject_t torqueSlopeDeciMilliNmPerSecond; // The slope in 0.1 mN·m per second
        lv_subject_t torqueDeciMilliNm;               // Torque from the measured current, 0.1 mN·m
        lv_subject_t torqueSlopePerMillePerSecond;    // Torque slope, 0.1 % per second
        lv_subject_t torqueSpeedLimitForwardRpm;      // 2007:10
        lv_subject_t torqueSpeedLimitReverseRpm;      // 2007:11
        lv_subject_t isStepTest;                      // A step test runs (PAR-010)
        lv_subject_t isStepTestPosition;              // 0 velocity steps, 1 position steps
        lv_subject_t stepTestVelocityARpm;            // Velocity steps alternate between A and B
        lv_subject_t stepTestVelocityBRpm;
        lv_subject_t stepTestStepMilliturns;           // Position step distance, 0.001 turn
        lv_subject_t stepTestDwellMs;                  // Time at each target
        lv_subject_t stepTestAccelerationRpmPerSecond; // The test's ramp
        lv_subject_t stepTestMoveSpeedRpm;             // Position steps' speed
        lv_subject_t unitsPerTurn;                     // 6064 user units per motor turn
        lv_subject_t velocityRpm;
        lv_subject_t motionSampleCount; // Motion samples taken so far, for the state log; charts fetch samples on their own timer
        lv_subject_t positionCounts;
        lv_subject_t currentCentiamps; // Converted from 6078 with the rated current
        lv_subject_t currentPerMille;  // 6078 as the drive reports it
        lv_subject_t busVoltageDeciV;
        lv_subject_t temperatureC;
        lv_subject_t undervoltageLimitV;
        lv_subject_t originalUndervoltageLimitV;
        lv_subject_t vendorId;
        lv_subject_t productCode;
        lv_subject_t serialNumber;
        lv_subject_t deviceName;      // String: 1008
        lv_subject_t hardwareVersion; // String: 1009
        lv_subject_t softwareVersion; // String: 100A
        lv_subject_t nodeId;
        lv_subject_t bitrateCode;
        lv_subject_t watchdogMs;
        lv_subject_t heartbeatMs;         // 1017 now (D-023)
        lv_subject_t originalHeartbeatMs; // 1017 as the drive came
        lv_subject_t encoderIncrements;
        lv_subject_t ratedCurrentCentiamps;
        lv_subject_t statorResistanceMilliohms;
        lv_subject_t quickStopDecelerationCounts;
        lv_subject_t quickStopOptionCode;
        lv_subject_t maxProfileVelocityCounts;
        lv_subject_t canErrorState;
        lv_subject_t busNodeId;        // Node the console talks to (D-026)
        lv_subject_t busBitrate;       // Bitrate the console runs at, bit/s
        lv_subject_t searchRoundCount; // Full sweeps without an answer since the last connect
        lv_subject_t canTransmitErrors;
        lv_subject_t canReceiveErrors;
        lv_subject_t canReceiveDrops;    // Frames lost to a full receive queue
        lv_subject_t faultRecordVersion; // Changes on every fault record read; the Faults screen fetches the record then
        lv_subject_t sessionLogVersion;
        lv_subject_t parametersVersion; // Changes when a parameter read finishes; the Parameters screen fetches the values then
        lv_subject_t parametersRead;    // Progress of the parameter read in progress
        lv_subject_t parametersTotal;
        lv_subject_t isReadingParameters;
        lv_subject_t unsavedChangeCount;     // Parameters written since the last save  // Changes on every session log change; the Faults screen fetches the log then
        lv_subject_t homingMethod;           // 6098 as read at connect; 21 = configured for a PTU
        lv_subject_t isParameterFlashBlank;  // The run checks found an erased parameter flash (D-031)
        lv_subject_t isRecoveredNotRestored; // RECOVER wrote the table's values; the restore hasn't succeeded yet (D-031)
        lv_subject_t needsPowerCycle;        // Motion refused until the motor boots again (D-031, D-032)
        lv_subject_t calibrationState;       // CalibrationStatus::State as int (D-032)
        lv_subject_t calibrationStage;       // 2009:06 while a calibration runs
        lv_subject_t calibrationVersion;     // Changes on every calibration update; the Calibrate tab fetches the status then
        lv_subject_t changedReactionCount;   // 200E entries that differ from the factory values
        lv_subject_t message;                // String: last event text
        lv_subject_t messageSequence;        // Changes on every new message, even a repeated one
        lv_subject_t messageLevel;           // MessageLevel as int; set before the sequence
    };

    /**
     * The console itself, from SystemStatus.
     */
    struct System {
        lv_subject_t isDisplayStarted;
        lv_subject_t isCanStarted;
    };

    /**
     * The latency measurement, from LatencyStatus (D-034). Levels are brightness, 0-4095, higher is brighter.
     */
    struct Latency {
        lv_subject_t state;     // LatencyStatus::State as int
        lv_subject_t kind;      // LatencyStatus::Kind as int
        lv_subject_t isLedOn;   // 0 or 1
        lv_subject_t level;     // Live 1 ms average
        lv_subject_t levelMin;  // Over the last 100 ms
        lv_subject_t levelMax;  // Over the last 100 ms
        lv_subject_t hasLevels; // The run learned its levels
        lv_subject_t darkLevel;
        lv_subject_t brightLevel;
        lv_subject_t threshold;
        lv_subject_t contrastPerMille;
        lv_subject_t sampleCount;
        lv_subject_t targetCount;
        lv_subject_t version; // Changes with samples, state, result or message; the screen fetches the status then
    };

    /**
     * Initialises every subject. Call once in the LVGL task (or with the display lock held), before any widget binds.
     */
    void init();

    /**
     * Copies the service snapshots into the subjects.
     *
     * @param motorStatus Latest motor snapshot
     * @param systemStatus Which parts of the console started
     */
    void update(const MotorStatus& motorStatus, const SystemStatus& systemStatus);

    /**
     * Copies the latency snapshot into the subjects.
     *
     * @param latencyStatus Latest latency snapshot
     */
    void updateLatency(const LatencyStatus& latencyStatus);

    /**
     * Logs every value, for the `state` serial command.
     */
    void log();

    // State groups
    Motor motor;
    System system;
    Latency latency;

  private:
    // Buffers for the message string subject
    char messageBuffer[sizeof(MotorStatus::message)] = "";
    char previousMessageBuffer[sizeof(MotorStatus::message)] = "";
    char deviceNameBuffer[sizeof(MotorStatus::deviceName)] = "";
    char previousDeviceNameBuffer[sizeof(MotorStatus::deviceName)] = "";
    char hardwareVersionBuffer[sizeof(MotorStatus::hardwareVersion)] = "";
    char previousHardwareVersionBuffer[sizeof(MotorStatus::hardwareVersion)] = "";
    char softwareVersionBuffer[sizeof(MotorStatus::softwareVersion)] = "";
    char previousSoftwareVersionBuffer[sizeof(MotorStatus::softwareVersion)] = "";

    /**
     * Copies text into a string subject only when it differs, since string subjects notify on every copy.
     *
     * @param subject String subject
     * @param text New text
     */
    static void copyStringIfChanged(lv_subject_t* subject, const char* text);
};
