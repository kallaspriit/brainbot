#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>

#include "drivers/CanBus.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "protocol/Cia402.hpp"
#include "protocol/ParameterTable.hpp"
#include "protocol/SdoClient.hpp"
#include "services/MotorStatus.hpp"

/**
 * The single owner of the CAN bus and the drive (D2 of the Navigation shell plan; STOP is a quick stop, D-008).
 *
 * One task finds the drive, reads its identity and configuration, polls its state at about 10 Hz, and runs the
 * commands the UI queues. STOP jumps the queue: it is a sticky flag that discards queued commands and cancels a
 * running sequence. Everything it writes is run-time only; nothing is saved to the drive. Nothing here touches LVGL.
 */
class MotorService {
  public:
    /**
     * Creates the service.
     *
     * @param sdo SDO client for the drive
     * @param bus CAN bus, for error counters and bus-off recovery
     */
    MotorService(SdoClient& sdo, CanBus& bus);

    /**
     * Starts the motor task.
     *
     * @return True if the task started
     */
    bool begin();

    // Commands, safe from any task; they only queue

    /**
     * Requests profile velocity mode at the target velocity and acceleration: state walk, then run.
     */
    void requestStartVelocity();

    /**
     * Requests a quick stop, then the power stage off. Discards queued commands and cancels a start in progress.
     */
    void requestStop();

    /**
     * Requests a fault reset.
     */
    void requestFaultReset();

    /**
     * Requests a gentle stop: target velocity 0 with the profile deceleration, then the power stage off once stopped.
     * Falls back to a quick stop if the ramp takes much longer than the deceleration allows. A STOP request
     * (requestStop) interrupts it.
     */
    void requestRampStop();

    /**
     * Sets the target velocity, applied immediately while running.
     *
     * @param velocityRpm Target velocity in rpm, clamped to ± the velocity limit (MotorStatus::velocityLimitRpm)
     */
    void setTargetVelocityRpm(int32_t velocityRpm);

    /**
     * Sets the profile acceleration and deceleration, applied immediately while running.
     *
     * @param accelerationRpmPerSecond Acceleration in rpm/s, clamped to MIN_ACCELERATION_RPM_PER_SECOND and the
     * acceleration limit (MotorStatus::accelerationLimitRpmPerSecond)
     */
    void setAccelerationRpmPerSecond(int32_t accelerationRpmPerSecond);

    // Position mode (Position mode plan)

    /**
     * Sets the pending position target; nothing moves until requestGoPosition() (D3).
     *
     * @param targetMilliturns Target from the console zero in 0.001 turn, clamped to ±MAX_POSITION_TARGET_TURNS
     */
    void setPositionTargetMilliturns(int32_t targetMilliturns);

    /**
     * Sets the move speed (6081) for the next GO.
     *
     * @param moveSpeedRpm Speed in rpm, clamped to 1 and the velocity limit
     */
    void setMoveSpeedRpm(int32_t moveSpeedRpm);

    /**
     * Chooses whether GO moves to the absolute target or by the relative distance (D4 of the Position mode plan).
     *
     * @param isRelative True for relative moves
     */
    void setPositionRelative(bool isRelative);

    /**
     * Sets the pending relative move; nothing moves until requestGoPosition().
     *
     * @param moveByMilliturns Distance in 0.001 turn, clamped to ±MAX_POSITION_TARGET_TURNS
     */
    void setMoveByMilliturns(int32_t moveByMilliturns);

    /**
     * Requests a move to the pending target: enables profile position mode first if needed (widening 607D when the
     * target needs it, D-020), then sends the target with "change set immediately". A GO during a move retargets.
     */
    void requestGoPosition();

    /**
     * Requests the console zero moved to where the shaft is now (D-021): positions, absolute targets and the chart
     * count from here. A console offset only, nothing is written to the drive, so it works stopped, holding or moving.
     */
    void requestZeroHere();

    /**
     * Requests a halt in position mode: decelerate on the profile ramp and hold position (controlword bit 8).
     */
    void requestHalt();

    /**
     * Requests a release in position mode: power stage off, shaft free, position mode ends.
     */
    void requestRelease();

    /**
     * Tells the service a motion screen opened: if the other mode is running, it ends (D-022). Velocity gets a ramp
     * stop; position gets a halt, then a release once still. A message says why. The matching mode is left alone.
     *
     * @param mode The screen's mode (MotionMode::VELOCITY or MotionMode::POSITION)
     */
    void requestMotionScreen(MotionMode mode);

    /**
     * Returns the least free stack the motor task has had, for the serial "stack" command.
     *
     * @return Bytes, 0 before the task runs
     */
    uint32_t getStackHighWaterBytes() const;

    // Torque mode (Torque mode plan)

    /**
     * Requests profile torque mode (6060 = 4): the slope, a zero target, the state walk, then the target torque. Takes
     * over from another mode as START does. The speed is held at the drive's torque speed limits (2007:10/11).
     */
    void requestStartTorque();

    /**
     * Requests a smooth stop of torque mode: the target goes to 0 at the slope, then a quick stop brakes the shaft to
     * standstill (a free shaft would coast from the speed limit otherwise) and the power stage goes off.
     */
    void requestTorqueStop();

    /**
     * Sets the target torque; applied at once while torque mode runs.
     *
     * @param torquePerMille Target in 0.1 % of rated, clamped to ±1000
     */
    void setTargetTorquePerMille(int32_t torquePerMille);

    /**
     * Sets the torque slope; applied at once while torque mode runs. A console setting, never saved.
     *
     * @param slopePerMillePerSecond Slope in 0.1 % per second, at least 1
     */
    void setTorqueSlopePerMillePerSecond(int32_t slopePerMillePerSecond);

    // Step test (PAR-010)

    /**
     * Sets the step test's kind: velocity steps between two speeds, or position steps of a distance and back.
     *
     * @param isPosition True for position steps
     */
    void setStepTestPosition(bool isPosition);

    /**
     * Sets the two speeds velocity steps alternate between; clamped to the velocity limit.
     *
     * @param velocityARpm First speed
     * @param velocityBRpm Second speed
     */
    void setStepTestVelocities(int32_t velocityARpm, int32_t velocityBRpm);

    /**
     * Sets how far position steps move, there and back from where the test starts.
     *
     * @param stepMilliturns Distance, 0.001 turn
     */
    void setStepTestStepMilliturns(int32_t stepMilliturns);

    /**
     * Sets the time at each target.
     *
     * @param dwellMs Dwell, clamped to 0.5-10 s
     */
    void setStepTestDwellMs(int32_t dwellMs);

    /**
     * Sets the test's ramp, for both kinds of step. Until set it is the steepest the test allows (30000 rpm/s). A ramp steeper than the drive allows raises max acceleration 60C5, max deceleration
     * 60C6 and the quick-stop deceleration 6085 for the test (run-time writes, put back when it ends), so one setting is enough and STOP stays the steepest ramp (D-019).
     *
     * @param accelerationRpmPerSecond Ramp, clamped to 10-30000 rpm/s
     */
    void setStepTestAccelerationRpmPerSecond(int32_t accelerationRpmPerSecond);

    /**
     * Sets the speed of position steps. Until set it is the speed limit (the rated speed).
     *
     * @param speedRpm Speed, clamped to the velocity limit
     */
    void setStepTestMoveSpeedRpm(int32_t speedRpm);

    /**
     * Requests a step test (PAR-010): the motor alternates between the two targets until the test is stopped, with the
     * test's own ramp and position speed (by default the steepest ramp and the speed limit, so the step tests the loop
     * and not the profile). Takes over motion already
     * running. Ends with STOP TEST, leaving the Tuning screen, the top bar STOP, a fault, a stall or a lost drive; the
     * Velocity and Position screens' targets are put back afterwards.
     */
    void requestStartStepTest();

    /**
     * Requests the step test ended: velocity ramps to 0, position halts; then the power stage goes off. Does nothing
     * when no test runs, so motion started elsewhere keeps running.
     */
    void requestStopStepTest();

    /**
     * Requests a dump of the drive's object dictionary to the log (FLT-001, temporary: removed once M6 has its factory
     * table). Probes the CiA 301, manufacturer and CiA 402 ranges and logs every object that answers. Reads only;
     * refused while the motor runs.
     */
    void requestDump();

    /**
     * Requests restore defaults (D-025): writes 1011:01 = "load" (with the rated voltage lowered first on a low supply),
     * checks the drive didn't refuse it, and applies the console's session settings again. Refused while the motor runs
     * or holds. The UI asks for confirmation first.
     */
    void requestRestoreDefaults();

    /**
     * Requests the blank-flash recovery (D-031): every read-write table entry that reads all ones gets the table's
     * factory value, then a fault reset, then restore defaults. Refused unless the run checks found a blank flash and
     * the motor is stopped and unpowered. The UI asks for confirmation first.
     */
    void requestRecover();

    /**
     * Makes the run checks report a blank flash from now on, so the recovery UI can be exercised on a healthy motor
     * (`fake blank` debug command). On a healthy motor the recovery then writes nothing and goes on to the restore.
     */
    void requestFakeBlank();

    /**
     * Writes all ones to every entry the recovery covers, at run time only, so the real recovery path can be tested on
     * a healthy motor (`blank` debug command): the run checks then find a blank flash and RECOVER writes the values
     * back. Nothing is saved; a motor power cycle undoes it too. Refused while the motor runs or is powered.
     */
    void requestBlankForTest();

    /**
     * Requests the drive's offline parameter identification (D-032, Motor calibration plan): initial angle to 0,
     * control mode 5, servo enable through virtual input 1, then the drive locks the shaft and spins both ways. The
     * identified constants land in the drive's RAM and in the calibration status; KEEP (save) or DISCARD (revert) and a
     * motor power cycle follow. Refused unless the drive is stopped and unpowered, not blank and not waiting for a power
     * cycle, and the pack is above the cutoff. The UI asks for confirmation first; the motor spins up to 80 % of its
     * rated speed.
     */
    void requestCalibration();

    /**
     * Starts the search past the default node and bitrate now, instead of after a few seconds without an answer
     * (D-026): every other bitrate, then node IDs 2-127, then the default again. A connected drive is forgotten first,
     * which also makes it a test of the whole search; refused while the motor runs.
     */
    void requestSearch();

    /**
     * THROWAWAY (test/gamepad): hands the bus to another user (the tank test) or takes it back. Suspending stops a
     * running motor, forgets the drive, parks the bus at the default node and bitrate, and then leaves the SDO client
     * alone until resumed; resuming searches from the default again.
     *
     * @param shouldSuspend True to hand the bus over, false to take it back
     */
    void setBusSuspended(bool shouldSuspend);

    /**
     * Returns whether the motor task has let go of the bus after setBusSuspended(true).
     *
     * @return True once the task no longer touches the bus
     */
    bool isBusSuspended() const;

    /**
     * A raw SDO transfer for the serial console (`sdo` debug command): one object read, string read or write, with the
     * result logged. For bench debugging of a drive the UI can't help with; writes are run-time unless the drive saves.
     */
    struct RawSdoRequest {
        enum class Kind : uint8_t {
            READ,        // Expedited read, logged as hex, unsigned and signed
            READ_STRING, // Segmented string read
            WRITE,       // Expedited write of sizeBytes
            NMT,         // NMT command to the drive; value holds the command specifier (0x81 resets the node, which reloads its flash)
        };

        Kind kind = Kind::READ;
        uint16_t index = 0;
        uint8_t subIndex = 0;
        uint8_t sizeBytes = 4; // WRITE only
        uint32_t value = 0;    // WRITE only
    };

    /**
     * Queues a raw SDO transfer; the result goes to the log. Writes to 200C:01 and the save and restore objects with
     * anything but their documented commands are refused (safety rules).
     *
     * @param request What to transfer
     */
    void requestRawSdo(const RawSdoRequest& request);

    // Parameters (PAR-002)

    /**
     * Requests a read of every parameter in the table (D6 of the Parameters plan). Reads run in small chunks between
     * polls, so STOP and the motion samples keep their pace; allowed while the motor runs. A new request restarts a
     * read in progress. Progress and completion are in MotorStatus (parametersRead, parametersVersion).
     *
     * @param shouldLog Log every value when done (the serial "params" command, for comparing with the factory dump)
     */
    void requestReadParameters(bool shouldLog = false);

    /**
     * Requests a read of one group's parameters, for after a write, a save or a restore.
     *
     * @param group Group number (into Parameters::GROUPS)
     */
    void requestReadParameterGroup(uint8_t group);

    /**
     * Copies the parameter values, one per Parameters::ENTRIES entry, same order.
     *
     * @param values Receives the values
     * @param maxCount Size of the values array
     * @return Number of values copied (Parameters::ENTRY_COUNT if the array is large enough)
     */
    size_t getParameterValues(ParameterValue* values, size_t maxCount);

    /**
     * Requests a run-time write of one parameter (PAR-004): the drive keeps it until a save or its power-off. Refused
     * for entries that aren't editable, and while the motor runs for those that apply only when stopped (D5 of the
     * Parameters plan). The outcome goes to the message line; the value is read back and the list rebuilt.
     *
     * @param position Entry position in Parameters::ENTRIES
     * @param raw Value as the drive holds it
     */
    void requestWriteParameter(size_t position, uint32_t raw);

    /**
     * Copies the parameters written since the last save.
     *
     * @param changes Receives the changes
     * @param maxCount Size of the changes array
     * @return Number of changes copied
     */
    size_t getUnsavedChanges(UnsavedChange* changes, size_t maxCount);

    /**
     * Requests a save of every parameter to the drive's flash (PAR-005, D-028): the console's own values go back first
     * (as the motor had them, or factory values when those are unknown), then 1010:01 = "save", then the console's
     * session settings again. Refused while the motor runs or holds. The UI confirms first, listing the changes and
     * getSavePreview().
     */
    void requestSave();

    /**
     * Requests the unsaved changes undone (PAR-009): each changed parameter written back to its value from before the
     * first write this session. Values that can't change while the motor runs are left and named in the message. The
     * user's tap on REVERT is the confirming action.
     */
    void requestRevert();

    /**
     * Says what a save would keep besides the user's changes, from the bus voltage now.
     *
     * @return The preview
     */
    SavePreview getSavePreview();

    // State

    /**
     * Returns a copy of the latest drive status.
     *
     * @return The status snapshot
     */
    MotorStatus getStatus();

    /**
     * Copies the motion samples taken after a given one, oldest first, for plots. Only the latest
     * MOTION_HISTORY_LENGTH are kept; older ones are skipped.
     *
     * @param fromSampleNumber Number of the first sample wanted (samples are numbered from 0, see
     * MotorStatus::motionSampleCount)
     * @param samples Receives the samples
     * @param maxCount Size of the samples array
     * @param nextSampleNumber Receives the number to pass next time
     * @return Number of samples copied
     */
    size_t getMotionSamples(uint32_t fromSampleNumber, MotionSample* samples, size_t maxCount, uint32_t& nextSampleNumber);

    /**
     * Copies the session log, newest first: EMCYs, resets and connects since the console started, with the motion at
     * the time (D-024). Only the latest SESSION_LOG_LENGTH are kept.
     *
     * @param entries Receives the entries
     * @param maxCount Size of the entries array
     * @return Number of entries copied
     */
    size_t getSessionLog(SessionLogEntry* entries, size_t maxCount);

    // Motion sampling for plots
    static constexpr uint32_t SAMPLE_INTERVAL_MS = 20;  // About 50 Hz; velocity, position and current reads each (D-017)
    static constexpr size_t MOTION_HISTORY_LENGTH = 64; // Over a second of samples, far more than the UI's 100 ms pulls
    static constexpr size_t SESSION_LOG_LENGTH = 32;    // Session log entries kept
    static constexpr size_t MAX_UNSAVED_CHANGES = 64;   // Parameter writes kept until a save

    // Limits for the UI
    static constexpr int32_t MIN_ACCELERATION_RPM_PER_SECOND = 10;
    static constexpr int32_t FALLBACK_VELOCITY_LIMIT_RPM = 300;                // Until the drive's rated speed is read
    static constexpr int32_t FALLBACK_ACCELERATION_LIMIT_RPM_PER_SECOND = 300; // Until 6085 is read
    static constexpr int32_t DEFAULT_ACCELERATION_RPM_PER_SECOND = 600;
    static constexpr int32_t DEFAULT_VELOCITY_RPM = 600;
    static constexpr int32_t DEFAULT_MOVE_SPEED_RPM = 600; // Position moves
    static constexpr int32_t DEFAULT_STEP_TEST_DWELL_MS = 2000;
    static constexpr int32_t DEFAULT_TORQUE_SLOPE_PER_MILLE_PER_SECOND = 1000; // 100 %/s; the factory 6087 is 1 %/s (D4 of the plan)
    static constexpr int32_t MAX_TORQUE_PER_MILLE = 1000;                      // ±100 % of rated
    static constexpr int32_t NOT_SET = -1;
    static constexpr int32_t RECENT_ALARM_MS = 2000;                            // An EMCY this recent explains a drive that switched itself off
    static constexpr int32_t MAX_STEP_TEST_ACCELERATION_RPM_PER_SECOND = 30000; // The PTU's quick stop: 5 000 000 counts/s²
    static constexpr int32_t MIN_STEP_TEST_DWELL_MS = 500;
    static constexpr int32_t MAX_STEP_TEST_DWELL_MS = 10000;
    static constexpr int32_t MAX_POSITION_TARGET_TURNS = 1000; // Targets are kept within this from the zero

  private:
    enum class Command : uint8_t {
        START_VELOCITY,
        FAULT_RESET,
        RAMP_STOP,
        GO_POSITION,
        HALT,
        RELEASE,
        ENTER_VELOCITY_SCREEN,
        ENTER_POSITION_SCREEN,
        ENTER_TORQUE_SCREEN,
        ZERO_HERE,
        DUMP,
        RESTORE_DEFAULTS,
        SAVE,
        REVERT,
        START_TORQUE,
        TORQUE_STOP,
        START_STEP_TEST,
        STOP_STEP_TEST,
        RAW_SDO,
        RECOVER,
        BLANK_FOR_TEST,
        CALIBRATE,
    };

    // Dependencies
    SdoClient& sdo;
    CanBus& bus;

    // Shared with other tasks
    QueueHandle_t commandQueue = nullptr;
    std::mutex rawSdoMutex;                       // Guards rawSdoRequest between the console task and the motor task
    RawSdoRequest rawSdoRequest;                  // The transfer a queued RAW_SDO command performs
    std::atomic<bool> isStopRequested {false};    // Sticky: no later command can replace it
    std::atomic<bool> isSearchRequested {false};  // Search past the default now (D-026)
    std::atomic<bool> isSuspendRequested {false}; // THROWAWAY (test/gamepad): another user wants the bus
    std::atomic<bool> isSuspended {false};        // The task has let go of the bus
    std::atomic<bool> isFakeBlank {false};        // Report a blank flash whatever the sentinels say (`fake blank`, D-031)
    std::atomic<int32_t> targetVelocityRpm {DEFAULT_VELOCITY_RPM};
    std::atomic<int32_t> targetAccelerationRpmPerSecond {DEFAULT_ACCELERATION_RPM_PER_SECOND};
    std::atomic<int32_t> velocityLimitRpm {FALLBACK_VELOCITY_LIMIT_RPM};
    std::atomic<int32_t> accelerationLimitRpmPerSecond {FALLBACK_ACCELERATION_LIMIT_RPM_PER_SECOND};
    std::atomic<int32_t> targetPositionMilliturns {0};
    std::atomic<int32_t> moveSpeedRpm {DEFAULT_MOVE_SPEED_RPM};
    std::atomic<bool> isPositionRelative {false};
    std::atomic<int32_t> moveByMilliturns {1000}; // One turn: the usual repeat-move test
    std::atomic<int32_t> targetTorquePerMille {0};
    std::atomic<int32_t> torqueSlopePerMillePerSecond {DEFAULT_TORQUE_SLOPE_PER_MILLE_PER_SECOND};
    std::atomic<bool> isStepTestPosition {false};
    std::atomic<int32_t> stepTestVelocityARpm {0};
    std::atomic<int32_t> stepTestVelocityBRpm {DEFAULT_VELOCITY_RPM};
    std::atomic<int32_t> stepTestStepMilliturns {1000};
    std::atomic<int32_t> stepTestDwellMs {DEFAULT_STEP_TEST_DWELL_MS};
    std::atomic<int32_t> stepTestAccelerationRpmPerSecond {NOT_SET}; // NOT_SET: MAX_STEP_TEST_ACCELERATION_RPM_PER_SECOND
    std::atomic<int32_t> stepTestMoveSpeedRpm {NOT_SET};             // NOT_SET: the speed limit
    std::mutex statusMutex;
    MotorStatus status;
    MotionSample motionHistory[MOTION_HISTORY_LENGTH] = {};           // Ring buffer, under statusMutex
    SessionLogEntry sessionLog[SESSION_LOG_LENGTH] = {};              // Ring buffer, under statusMutex
    uint32_t sessionLogCount = 0;                                     // Entries added so far, under statusMutex
    ParameterValue parameterValues[Parameters::ENTRY_COUNT] = {};     // One per table entry, under statusMutex
    std::atomic<int32_t> requestedParameterGroup {NO_PARAMETER_READ}; // Read to start: a group, ALL_PARAMETERS or none
    bool hasPendingWrite = false;                                     // A parameter write waits, under statusMutex
    size_t pendingWritePosition = 0;
    uint32_t pendingWriteRaw = 0;
    UnsavedChange unsavedChanges[MAX_UNSAVED_CHANGES] = {}; // Under statusMutex; the count is in the status

    // Task state, not shared
    int32_t appliedVelocityRpm = 0;
    int32_t appliedTorquePerMille = 0; // 6071 as last written while torque mode runs
    int32_t appliedTorqueSlope = 0;    // 6087 as last written
    int32_t appliedAccelerationRpmPerSecond = 0;
    int32_t sentTargetUnits = 0;              // Last target the drive acknowledged, in user units
    int32_t sentMoveSpeedRpm = 0;             // Move speed sent with it
    int32_t sentAccelerationRpmPerSecond = 0; // Acceleration applied when it was sent
    bool isAwaitingSettle = false;            // A move was sent and hasn't reported where it settled
    bool hasLeftTarget = false;               // Target reached went low after the set-point, so a rise means arrival
    int32_t lastSeenMoveSpeedRpm = 0;         // For settling: the values last seen and when they changed
    int32_t lastSeenAccelerationRpmPerSecond = 0;
    int64_t lastProfileChangeUs = 0;
    uint32_t encoderIncrements = 0; // Copy of the status value, for unit conversions
    int failedPollCount = 0;
    int lowVoltagePollCount = 0;       // Consecutive polls below the pack cutoff
    uint32_t watchdogMs = 0;           // Drive's communication watchdog time, 0 = off
    bool isFaultRecordStale = false;   // An EMCY, a new error code or a fault reset since the last fault record read
    bool isStallChecked = false;       // The current stall's reaction was checked; cleared when the stall ends
    TaskHandle_t taskHandle = nullptr; // The motor task, for its stack high-water mark
    bool hasWarnedStack = false;       // The low-stack warning was logged
    uint16_t lastAlarmCode = 0;        // Last non-zero EMCY code, for why a drive switched itself off
    int64_t lastAlarmUs = 0;           // When it came

    // Step test (PAR-010), motor task only
    bool isStepTestAtB = false;   // Heading for or at target B (velocity) or the far end (position)
    int64_t nextStepUs = 0;       // When the next step goes
    int32_t savedVelocityRpm = 0; // Velocity screen's target, put back when the test ends
    int32_t savedAccelerationRpmPerSecond = 0;
    bool savedIsPositionRelative = false; // Position screen's settings, put back when the test ends
    int32_t savedMoveByMilliturns = 0;
    int32_t savedMoveSpeedRpm = 0;
    bool hasRaisedRampLimits = false;        // 60C5, 60C6 and 6085 were read and may be raised for the test
    uint32_t savedMaxAccelerationCounts = 0; // The three as the drive had them, put back when the test ends
    uint32_t savedMaxDecelerationCounts = 0;
    uint32_t savedQuickStopCounts = 0;
    uint32_t raisedRampCounts = 0;  // What the three are at least now
    uint32_t searchStep = 0;        // Where the search is (D-026); 0 = the default node and bitrate
    int defaultMissCount = 0;       // Failed connects at the default before the search moves on
    bool shouldResetSearch = false; // The drive was lost: go back to the default before searching again
    int64_t lastFlashWriteUs = -1;  // When the last save or restore was written; its pause raises 0x7501
    int64_t lastWatchdogFeedUs = 0; // When the drive's watchdog was last fed
    bool isParameterReadActive = false;
    std::atomic<bool> shouldLogParameters {false};
    size_t parameterReadErrorCount = 0;
    size_t parameterReadCursor = 0; // Next entry the parameter read looks at
    int32_t parameterReadGroup = 0; // Group being read, or ALL_PARAMETERS
    int64_t parameterReadStartUs = 0;

    /**
     * Task entry point.
     *
     * @param context The MotorService instance
     */
    static void taskMain(void* context);

    /**
     * Task loop: connect, stop, commands, follow targets, poll.
     */
    void run();

    /**
     * Reads identity and configuration, stops a drive left running, and lowers the undervoltage limit (D-005).
     *
     * @return True if the drive answered
     */
    bool connect();

    /**
     * Sets the acceleration limit from the quick-stop deceleration (D-019) and clamps the acceleration target to it.
     * Called at connect and whenever 6085 is written, so the limit follows a change at once.
     *
     * @param quickStopDecelerationCounts 6085 in user units/s², 0 if unread
     */
    void applyQuickStopLimit(uint32_t quickStopDecelerationCounts);

    /**
     * Sets the velocity and acceleration limits from the drive's values and clamps the current targets to them.
     *
     * @param ratedSpeedRpm 2000:0E, 0 if unread
     * @param maxMotorSpeedRpm 6080, 0 if unread
     * @param maxProfileVelocityCounts 607F, 0 if unread
     * @param quickStopDecelerationCounts 6085, 0 if unread
     */
    void updateLimits(uint32_t ratedSpeedRpm, uint32_t maxMotorSpeedRpm, uint32_t maxProfileVelocityCounts, uint32_t quickStopDecelerationCounts);

    /**
     * Reads the name and version strings (1008-100A) into the status.
     */
    void readIdentityStrings();

    /**
     * Lowers the bus undervoltage limit (2001:10) to UNDERVOLTAGE_LIMIT_FOR_BATTERY_V if it is higher. Run-time write,
     * not saved; the value the drive came with stays in the status. Call only with the drive switched off.
     */
    void lowerUndervoltageLimitIfNeeded();

    /**
     * Turns on the drive's heartbeat (1017) so a pulled cable stops the motor (D-023): with nobody acknowledging the
     * heartbeat the drive raises 0x7501 and reacts as 200E:09 says. Run-time write, not saved; the value the drive came
     * with stays in the status. Also reads 200E:09 and warns if that reaction doesn't stop the motor.
     */
    void enableHeartbeat();

    /**
     * Reads the error register, the alarm history and the fault reactions into the status (D-024). At connect it runs
     * before any write, so the history is seen as the motor came.
     */
    void readFaultRecord();

    /**
     * Reads the settings the run checks look at, as the motor came (FLT-005). Call at connect before any write.
     */
    void readRunCheckValues();

    /**
     * Captures the motor's own value of the entries the console writes: session values (D-005, D-020, D-023, D-025) and
     * the motion values the motion screens write on every start (D-028). Call at connect before any write; clears the
     * parameter values from the last drive.
     */
    void captureOwnParameterValues();

    /**
     * Starts a parameter read of a group, or of all, replacing one in progress.
     *
     * @param group Group number, or ALL_PARAMETERS
     */
    void startParameterRead(int32_t group);

    /**
     * Reads the next chunk of a parameter read in progress; finishes it at the end, or when the drive stops answering.
     */
    void readParameterChunk();

    /**
     * Logs every parameter value read, one line each, in the factory dump's format (the serial "params" command).
     */
    void logParameterValues();

    /**
     * Writes a requested parameter, reads it back and records it as an unsaved change (PAR-004).
     *
     * @param position Entry position in Parameters::ENTRIES
     * @param raw Value to write
     * @param isRevert True when writing an unsaved change's earlier value back: a locked entry (D-029) is allowed then,
     * since only a calibration (D-032) puts one on the list
     */
    void writeParameter(size_t position, uint32_t raw, bool isRevert = false);

    /**
     * Records a written value in the unsaved changes: adds it, updates it, or removes it when it is back at the value
     * before the first write. Call with statusMutex held.
     *
     * @param position Entry position
     * @param before Value before this write
     * @param now Value read back
     */
    void recordUnsavedChange(size_t position, uint32_t before, uint32_t now);

    /**
     * Forgets the unsaved changes, saying why if there were any.
     *
     * @param reason Why they are gone, for the message line
     */
    void clearUnsavedChanges(const char* reason);

    /**
     * Saves the parameters (PAR-005): puts the console's own values back, lowers 2000:07 if the bus needs it, writes
     * 1010:01, checks for a save fault and applies the session settings again.
     */
    void saveParameters();

    /**
     * Writes every unsaved change back to its earlier value (PAR-009); see requestRevert().
     */
    void revertParameters();

    /**
     * Starts the step test with the settings from the setters (PAR-010); see requestStartStepTest().
     */
    void startStepTest();

    /**
     * Steps the running test when its dwell is over, and ends it when the motor stopped for any reason.
     */
    void runStepTest();

    /**
     * Ends the step test: ramp or halt, power stage off, settings put back.
     */
    void stopStepTest();

    /**
     * Marks the step test over and puts the motion screens' targets back.
     */
    void endStepTest();

    /**
     * Moves the console zero to the current position; see requestZeroHere().
     */
    void zeroHere();

    /**
     * Starts profile torque mode (Torque mode plan); see requestStartTorque().
     *
     * @return True if the motor is running in torque mode
     */
    bool startTorque();

    /**
     * Follows target and slope changes while torque mode runs.
     */
    void followTorque();

    /**
     * Reads the torque speed limits 2007:10/11 into the status, for the Torque screen.
     */
    void readTorqueSpeedLimits();

    /**
     * Ends torque mode smoothly; see requestTorqueStop().
     */
    void torqueStop();

    /**
     * Returns the step test's ramp: as set, or the steepest the test allows.
     *
     * @return Acceleration, rpm/s
     */
    int32_t getStepTestAccelerationRpmPerSecond() const;

    /**
     * Returns the position steps' speed: as set, or the speed limit.
     *
     * @return Speed, rpm
     */
    int32_t getStepTestMoveSpeedRpm() const;

    /**
     * Applies the test's ramp and speed to the motion targets, at the start and before each step, so changes apply
     * from the next step.
     */
    void applyStepTestProfile();

    /**
     * Raises 60C5, 60C6 and 6085 to the test's ramp where they are lower, reading and keeping their values first.
     *
     * @param rampCounts The ramp in user units/s²
     * @return False if a read or write failed (the message says which)
     */
    bool raiseRampLimits(uint32_t rampCounts);

    /**
     * Puts 60C5, 60C6 and 6085 back as they were before the test.
     */
    void restoreRampLimits();

    /**
     * Writes the value a console-written entry goes back to before a save: the motor's own, or the factory value when
     * the own values are unknown. Leaves the undervoltage limit low when the bus is too low for the motor's own.
     *
     * @param preview What the save keeps
     * @return False if a write failed (the save stops)
     */
    bool putBackOwnValues(const SavePreview& preview);

    /**
     * Points the bus and the SDO client at one step of the search (D-026): step 0 is node 1 at 1 Mbit/s, then node 1
     * at each other bitrate, then node IDs 2-127 at 1 Mbit/s.
     *
     * @param step Search step, 0 to SEARCH_STEP_COUNT - 1
     */
    void applySearchStep(uint32_t step);

    /**
     * Writes restore defaults if the drive is switched off, reports a refusal, and applies the session settings again.
     *
     * @return True if the drive confirmed the restore (D-031: 603F without a save fault and 1011:01 = 1)
     */
    bool restoreDefaults();

    /**
     * The blank-flash recovery (D-031): writes the table's factory value to every read-write entry that reads all ones
     * (locked entries included, the bus settings, save and restore objects, command objects and console-managed values
     * excepted), resets the fault, then restores defaults. Marks the drive "recovered, not restored" while the table's
     * values are in it, which refuses SAVE.
     */
    void recoverBlankFlash();

    /**
     * The `blank` test command: all ones into every entry the recovery covers, then the run checks again.
     */
    void blankParametersForTest();

    /**
     * The calibration run (D-032): preconditions, the mode 5 sequence, polling until the drive finishes or fails, STOP
     * in between, and the drive back in CiA 402 mode with the virtual input as it was. Blocks the motor task for the
     * run (15-30 s), polling the drive itself.
     */
    void runCalibration();

    /**
     * Publishes the calibration status to the UI (copies it under the lock and bumps the version).
     *
     * @param calibration The status to publish
     */
    void publishCalibration(const CalibrationStatus& calibration);

    /**
     * Ends a calibration run: virtual input off, the drive back in its control mode, the before values written back
     * unless the run succeeded, a fault reset when the drive is in Fault, and the power-cycle mark.
     *
     * @param calibration The run's status, updated with the outcome
     * @param ownControlMode 2002:01 as the motor had it
     * @param ownVdiFunction 2017:01 as the motor had it
     * @param ownVdiLogic 2017:02 as the motor had it
     */
    void endCalibration(CalibrationStatus& calibration, uint32_t ownControlMode, uint32_t ownVdiFunction, uint32_t ownVdiLogic);

    /**
     * Sets the message for a save or restore the drive refused, naming the likely cause: a blank flash, a supply below
     * the flash-write guard, or neither.
     *
     * @param action "save" or "restore"
     * @param statusObject The object read for the outcome, e.g. "1010:01"
     * @param statusValue What it read; 1 is success
     * @param busVoltageDeciV The bus voltage at the time
     */
    void reportFlashRefusal(const char* action, const char* statusObject, uint32_t statusValue, int32_t busVoltageDeciV);

    /**
     * Resets a fault (controlword 0x80, then switched off) and says how it went: cleared, or why not (FLT-004).
     */
    void resetFault();

    /**
     * Sends a fault reset (controlword bit 7) and holds it for FAULT_RESET_HOLD_MS, so the drive sees the edge. The
     * caller writes the next controlword.
     *
     * @return True if the write succeeded
     */
    bool sendFaultReset();

    /**
     * Adds a session log entry with the time and the motion now, or merges it into the newest one if that is the same
     * within a second. Call holding statusMutex.
     *
     * @param entry The entry; time, mode and motion are filled in here
     */
    void addSessionLogEntry(SessionLogEntry entry);

    /**
     * Logs every object in the probed index ranges that the drive answers (FLT-001, temporary).
     */
    void dumpObjects();

    /**
     * Reads one object for the dump and logs it.
     *
     * @param index Object index
     * @param subIndex Sub-index
     * @param abortCode Receives the abort code when the read was aborted, else 0
     * @return True if the read returned a value
     */
    bool dumpObject(uint16_t index, uint8_t subIndex, uint32_t& abortCode);

    /**
     * Performs the queued raw SDO transfer and logs its result (`sdo` debug command).
     */
    void performRawSdo();

    /**
     * Reads state and measurements into the status. A failed read keeps the previous value.
     *
     * @return True if the statusword read succeeded
     */
    bool poll();

    /**
     * Marks the drive lost after repeated failed polls.
     */
    void handleLostDrive();

    /**
     * Quick-stops a running motor and refuses motion while the bus voltage is under the pack cutoff (D-005).
     */
    void checkPackVoltage();

    /**
     * Stops a run on a stall when the drive's stall reaction is halt and hold (the factory one): it keeps holding at up
     * to the rated current with no fault, so the console powers the stage off itself (bench 2026-10-11). Other
     * reactions are left to the drive: a fault stops it already, and a warning or ignore was chosen on purpose (the PTU
     * configures a warning). Reads the reaction once per stall, so a change on the Setup tab counts.
     */
    void checkStall();

    /**
     * Tells whether an entry is the node ID (200C:02) or bitrate (200C:03), the settings only the Setup danger group
     * writes (PAR-008).
     *
     * @param entry Table entry
     * @return True for 200C:02 and 200C:03
     */
    static bool isBusSettingEntry(const Parameters::Entry& entry);

    /**
     * Checks a node ID or bitrate write: motor stopped and unpowered, value in range, and the other setting at its
     * default unless this one goes back to its own, so the search (D-026) can find the motor after the power cycle.
     * Sets the refusal as the message.
     *
     * @param entry The node ID or bitrate entry
     * @param raw The value to write
     * @return True if the write may go ahead
     */
    bool canWriteBusSetting(const Parameters::Entry& entry, uint32_t raw);

    /**
     * Checks that motion may start: pack above the cutoff, encoder resolution known. Sets the refusal as the message.
     *
     * @param current Latest status
     * @return True if motion may start
     */
    bool canStartMotion(const MotorStatus& current);

    /**
     * Configures an operation mode and walks the CiA 402 state machine to Operation Enabled: CiA 402 control mode,
     * ramps, 6060, a fault reset if faulted, then the state walk. Switches the power stage off again on failure.
     *
     * @param operationMode Value for 6060
     * @param modeStepName Name of the mode write, for messages
     * @return True if the drive reached Operation Enabled
     */
    bool enableOperation(uint8_t operationMode, const char* modeStepName);

    /**
     * Runs the velocity start: settings, CiA 402 state walk, target velocity.
     *
     * @return True if the drive is running
     */
    bool startVelocity();

    /**
     * Runs a GO in position mode: enable profile position if needed, widen 607D if needed, send the target with
     * the set-point handshake (D7 of the Position mode plan).
     *
     * @return True if the drive took the target
     */
    bool goPosition();

    /**
     * Halts a position move and holds (controlword bit 8).
     */
    void haltPosition();

    /**
     * Switches the power stage off and leaves position mode; the shaft turns freely.
     */
    void releasePosition();

    /**
     * Ends the running mode if it isn't the wanted one (D-022): a ramp stop for velocity, halt and release for
     * position.
     *
     * @param wantedMode Mode that is about to take control
     * @param reason Why, for the message (e.g. "Position screen opened")
     * @return False if the other mode couldn't be ended (a STOP or a failed write); the caller should not go on
     */
    bool endOtherMode(MotionMode wantedMode, const char* reason);

    /**
     * Halts a position move, waits for standstill on the profile deceleration, then releases. Gives up on STOP or the
     * time limit, leaving STOP to the task loop.
     *
     * @return True if released
     */
    bool haltAndRelease();

    /**
     * Reports where a move settled once statusword bit 10 (target reached) rises after it, as the message. Called
     * after each poll.
     */
    void checkSettled();

    /**
     * Sends move speed (6081) and a target (607A) with the set-point handshake (D7 of the Position mode plan), and
     * records them as what the drive is moving with.
     *
     * @param targetUnits Absolute target in user units
     * @param speedRpm Move speed in rpm
     * @param unitsPerTurn User units per motor turn
     * @return True if the drive acknowledged the set-point
     */
    bool sendSetPoint(int32_t targetUnits, int32_t speedRpm, int32_t unitsPerTurn);

    /**
     * Returns whether the last sent target is a valid base for a relative move: the drive is moving to it or holding
     * it in position mode, not halted, released or faulted.
     *
     * @param current Latest status
     * @return True if relative moves continue from the last target
     */
    bool hasRelativeBase(const MotorStatus& current) const;

    /**
     * Applies move speed and acceleration changes in position mode once they have settled: re-sends the current
     * target while moving so the drive re-plans with them, or keeps them for the next GO while holding.
     *
     * @param running Latest status, in position mode
     */
    void followPositionProfile(const MotorStatus& running);

    /**
     * Widens the software position limits (607D) to nearly the full int32 range when a target lies outside them
     * (D-020). Run-time write, never saved.
     *
     * @param targetUnits Absolute target in user units
     * @return True if the target is inside the limits now
     */
    bool widenPositionLimitsIfNeeded(int32_t targetUnits);

    /**
     * Waits for statusword bit 12 (set-point acknowledge) after a new set-point. Gives up on STOP or the timeout.
     *
     * @param statusword Receives the last statusword read, for messages
     * @return True if the drive acknowledged
     */
    bool waitForSetPointAcknowledge(uint32_t& statusword);

    /**
     * Waits POSITION_ENABLE_SETTLE_MS after entering Operation Enabled in position mode, before the first set-point.
     * Gives up on STOP.
     *
     * @return False if STOP was requested
     */
    bool waitAfterEnable();

    /**
     * Quick-stops the drive (6040 = 0x02), waits for standstill, then switches the power stage off. If both writes fail,
     * the run stays marked running and the message shows the error.
     *
     * @param reason Text for the message, e.g. "Stopped"
     * @param level How serious the stop message is
     * @return True if the drive took either write
     */
    bool stopMotion(const char* reason, MessageLevel level = MessageLevel::INFO);

    /**
     * Ramps the motor to standstill with the profile deceleration, then switches the power stage off. Quick-stops if
     * the ramp overruns its time limit; returns early if STOP is requested, which the task loop then handles.
     */
    void rampStop();

    /**
     * Waits until the next poll, reading the velocity every SAMPLE_INTERVAL_MS for plots. Returns early when STOP is
     * requested, so the stop isn't held up by the poll interval.
     */
    void sampleUntilNextPoll();

    /**
     * Reads velocity, position and current and adds them to the motion history (three SDO reads, D-017).
     *
     * @param velocityRpm Receives the measured velocity
     * @return True if the velocity was read; a failed position or current read still records the sample, with a gap
     */
    bool sampleMotion(int32_t& velocityRpm);

    /**
     * Adds a motion sample to the history, with the targets the drive is following.
     *
     * @param velocityRpm Measured velocity
     * @param hasPosition Whether the position was read
     * @param positionUnits Measured position in user units (6064)
     * @param hasCurrent Whether the current was read
     * @param currentPerMille Measured current, per mille of the rated current (6078)
     */
    void recordMotionSample(int32_t velocityRpm, bool hasPosition, int32_t positionUnits, bool hasCurrent, int32_t currentPerMille);

    /**
     * Stores a measured current and its value in amps. Call holding statusMutex.
     *
     * @param currentPerMille Current, per mille of the rated current (6078)
     */
    void setCurrent(int32_t currentPerMille);

    /**
     * Stores a measured position and what follows from it: turns and degrees from the console zero, and the base of a
     * relative move. Call holding statusMutex.
     *
     * @param positionUnits Position in user units (6064)
     */
    void setPosition(int32_t positionUnits);

    /**
     * Converts a position in user units to milliturns from the console zero. Call holding statusMutex, with
     * status.unitsPerTurn above zero.
     *
     * @param positionUnits Position in user units
     * @return Milliturns from the console zero
     */
    int32_t toMilliturns(int32_t positionUnits);

    /**
     * Writes the profile acceleration and deceleration from the target.
     *
     * @return True on success
     */
    bool applyAcceleration();

    /**
     * Writes the controlword and waits for the drive to reach a state. Gives up if STOP is requested.
     *
     * @param controlword Controlword to write
     * @param expectedState State to wait for
     * @param stepName Name for messages
     * @return True if the state was reached
     */
    bool stepTo(uint16_t controlword, DriveState expectedState, const char* stepName);

    /**
     * Writes an object and records the error as the message on failure.
     *
     * @param entry Object to write
     * @param value Value
     * @param stepName Name for messages
     * @return True on success
     */
    bool writeObject(const ObjectEntry& entry, uint32_t value, const char* stepName);

    /**
     * Resets the task watchdog and feeds the drive's communication watchdog if due. Call from every loop and wait.
     */
    void keepAlive();

    /**
     * Records an EMCY message from the drive. Called by the SDO client during transfers.
     *
     * @param frame A received frame that wasn't an SDO response
     */
    void handleFrame(const CanFrame& frame);

    /**
     * Sets the status message and bumps its sequence number, so the UI shows it even if the text repeats.
     *
     * @param level How serious the message is
     * @param format printf-style format
     */
    void setMessage(MessageLevel level, const char* format, ...);

    /**
     * Converts rpm to the drive's velocity unit (encoder counts per second).
     *
     * @param velocityRpm Velocity in rpm
     * @return Velocity in counts per second
     */
    int32_t rpmToCountsPerSecond(int32_t velocityRpm) const;

    // Constants
    static constexpr uint32_t POLL_INTERVAL_MS = 100;                // Status refresh
    static constexpr uint32_t RECONNECT_INTERVAL_MS = 500;           // Between connect attempts
    static constexpr uint32_t STATE_TIMEOUT_MS = 1000;               // Max wait for each state walk step
    static constexpr uint32_t QUICK_STOP_TIMEOUT_MS = 3000;          // Max wait for standstill; 0.55 s from 300 rpm (D-008)
    static constexpr uint32_t RAMP_STOP_MARGIN_MS = 1000;            // Added to 1.5× the expected ramp time
    static constexpr uint32_t SET_POINT_TIMEOUT_MS = 500;            // Max wait for statusword bit 12 after a new target
    static constexpr uint32_t POSITION_ENABLE_SETTLE_MS = 100;       // After enabling, before the first set-point (bench: < 40 ms ignores the profile)
    static constexpr uint32_t PROFILE_CHANGE_SETTLE_MS = 200;        // Speed or ramp unchanged this long before a mid-move re-send
    static constexpr int32_t POSITION_LIMIT_MARGIN_UNITS = 1 << 24;  // Widened 607D stays this far inside int32 (0x8616)
    static constexpr int32_t STOPPED_THRESHOLD_RPM = 2;              // Below this the motor counts as stopped
    static constexpr int MAX_FAILED_POLLS = 3;                       // Consecutive failed polls before disconnecting
    static constexpr int LOW_VOLTAGE_POLLS = 3;                      // Consecutive low readings before the cutoff acts
    static constexpr uint32_t EXPECTED_VENDOR_ID = 0x509;            // NiMotion, observed by the PTU firmware
    static constexpr uint32_t UNDERVOLTAGE_LIMIT_FOR_BATTERY_V = 12; // Same value the PTU's motor setup writes
    static constexpr uint16_t HEARTBEAT_INTERVAL_MS = 100;           // Drive heartbeat; a lost bus is noticed within about this (D-023)
    static constexpr uint32_t FAULT_RESET_SETTLE_MS = 200;           // Wait before reading what a fault reset left
    static constexpr uint32_t FAULT_RESET_HOLD_MS = 60;              // Bit 7 stays set this long (whole sample intervals)
    static constexpr uint32_t RESTORE_RATED_VOLTAGE_V = 20;          // 2000:07 for a restore on a pack: the flash guard then allows 16 V
    static constexpr int32_t FLASH_WRITE_MIN_PERCENT = 80;           // Flash writes need the bus at this share of 2000:07 (inferred, bench 2026-10-11)
    static constexpr uint32_t RESTORE_CHECK_DELAY_MS = 300;          // Wait before reading whether the drive refused a restore
    static constexpr int DEFAULT_MISSES_BEFORE_SEARCH = 6;           // About 3 s at the default before trying anything else
    static constexpr int SEARCH_SDO_TIMEOUT_MS = 30;                 // A drive answers within a few ms; a scan step waits this long
    static constexpr uint8_t MAX_NODE_ID = 127;
    static constexpr uint32_t SEARCH_BITRATE_COUNT = 8;                                         // Other bitrates, in SEARCH_BITRATES
    static constexpr uint32_t SEARCH_STEP_COUNT = 1 + SEARCH_BITRATE_COUNT + (MAX_NODE_ID - 1); // Default, bitrates, node IDs 2-127
    static constexpr uint32_t FLASH_WRITE_QUIET_MS = 5000;                                      // After a flash write, an EMCY 0x7501 doesn't replace the message
    static constexpr uint32_t EMCY_COB_ID = 0x080;                                              // Plus the node ID
    static constexpr int32_t NO_PARAMETER_READ = -2;
    static constexpr int32_t ALL_PARAMETERS = -1;
    static constexpr size_t PARAMETER_READ_CHUNK_RUNNING = 20;  // Reads per loop pass while running: about 20 ms, one sample interval
    static constexpr size_t PARAMETER_READ_CHUNK_STOPPED = 100; // Reads per loop pass when stopped
    static constexpr uint32_t TASK_STACK_BYTES = 12288;         // 6144 overflowed ending position mode for torque (2026-10-13): each level copies MotorStatus
    static constexpr uint32_t STACK_WARNING_BYTES = 1024;       // Logged once if the free stack ever drops below this
    static constexpr UBaseType_t TASK_PRIORITY = 5;
    static constexpr UBaseType_t COMMAND_QUEUE_LENGTH = 4;
    static constexpr uint32_t BITRATE_CODE_1M = 8; // 200C:03 code for 1 Mbit/s, the default (manual §3.6.1)
};
