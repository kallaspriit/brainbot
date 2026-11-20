#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>

#include "driver/gpio.h"
#include "drivers/LightSensor.hpp"
#include "esp_ldo_regulator.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "services/LatencyStatus.hpp"

/**
 * Glass-to-glass latency measurement (D-034, Latency measurement plan): an LED in front of the camera, the light sensor
 * on the operator's screen. Independent of the motor and the CAN bus.
 *
 * A run learns the dark and bright levels in its first cycle (D5), then times event-driven cycles with random holds
 * (D4) on the 1 ms moving average of the sensor (D6), and publishes the samples and statistics (D8). The self-test is
 * the same run with the sensor against the LED (D7). One task does all of it; commands only queue.
 */
class LatencyService {
  public:
    /**
     * Creates the service.
     *
     * @param sensor The light sensor; started and stopped by this service
     * @param pinLed The LED's output pin
     */
    LatencyService(LightSensor& sensor, gpio_num_t pinLed);

    /**
     * Sets up the LED pin (off) and starts the task.
     *
     * @return True if the task started
     */
    bool begin();

    // Commands, safe from any task; they only queue

    /**
     * Starts the sensor for aiming: the live level and manual LED control. Keeps a finished run's result.
     */
    void requestAim();

    /**
     * Starts a measurement run of DEFAULT_SAMPLE_COUNT samples.
     */
    void requestMeasurement();

    /**
     * Starts a self-test of SELF_TEST_SAMPLE_COUNT samples, with the sensor pressed against the LED.
     */
    void requestSelfTest();

    /**
     * Ends a run with the samples it has; outside a run, nothing.
     */
    void requestStop();

    /**
     * Stops the sensor, turns the LED off and forgets nothing: the last result stays.
     */
    void requestIdle();

    /**
     * Switches the LED while aiming; ignored during a run.
     *
     * @param isOn True to light it
     */
    void setLedOn(bool isOn);

    /**
     * Fills a run with synthetic samples, for checking the screen without a sensor (`latency demo`): a run in
     * progress with 40 of 100 samples; STOP ends it and shows the result. Debug only.
     */
    void requestDemo();

    /**
     * Logs the live level to the serial console ten times a second while the sensor runs (`latency level`).
     *
     * @param isLogged True to log
     */
    void setLevelLogged(bool isLogged);

    // State

    /**
     * Returns a copy of the latest status.
     *
     * @return The status snapshot
     */
    LatencyStatus getStatus();

    // Constants
    static constexpr uint16_t DEFAULT_SAMPLE_COUNT = 100;   // LED-on edges (D-035)
    static constexpr uint16_t SELF_TEST_SAMPLE_COUNT = 200; // About 10 s with the self-test's short holds
    static constexpr uint32_t SELF_TEST_PASS_US = 1000;     // The console's own chain must be under 1 ms

  private:
    enum class Command : uint8_t {
        AIM,
        MEASURE,
        SELF_TEST,
        STOP,
        IDLE,
        LED_ON,
        LED_OFF,
        DEMO,
    };

    enum class LearnPhase : uint8_t {
        DARK,   // LED off, settling, then the dark level
        BRIGHT, // LED on, waiting for the screen to change and settle
    };

    enum class CyclePhase : uint8_t {
        HOLD,      // LED steady until holdEndUs
        WAIT_EDGE, // LED just switched; waiting for the screen to follow
    };

    // Dependencies
    LightSensor& sensor;
    gpio_num_t pinLed;
    esp_ldo_channel_handle_t ledPowerChannel = nullptr; // LDO 4, which powers the LED pin's IO domain

    // Shared with other tasks
    QueueHandle_t commandQueue = nullptr;
    std::atomic<bool> isLevelLogged {false};
    std::mutex statusMutex;
    LatencyStatus status;

    // Task state, not shared: the published status is copied from these
    LatencyStatus::State state = LatencyStatus::State::IDLE;
    LatencyStatus::Kind kind = LatencyStatus::Kind::MEASUREMENT;
    bool isLedOn = false;

    // 1 ms moving average (plan D6)
    uint16_t averageWindow[20] = {};
    uint32_t averageSum = 0;
    size_t averageIndex = 0;
    size_t averageFill = 0;

    // Live level per publish interval
    uint16_t level = 0;
    uint16_t levelMin = UINT16_MAX;
    uint16_t levelMax = 0;
    int64_t levelPublishedUs = 0;
    int64_t levelLoggedUs = 0;

    // History at 1 kHz for the learning cycle: (time, level)
    static constexpr size_t HISTORY_LENGTH = 4096; // About 4 s
    int64_t historyTimeUs[HISTORY_LENGTH] = {};
    uint16_t historyLevel[HISTORY_LENGTH] = {};
    size_t historyCount = 0; // Entries written since the run started; the ring holds the newest HISTORY_LENGTH
    uint32_t historyDecimation = 0;

    // Learning
    LearnPhase learnPhase = LearnPhase::DARK;
    int64_t phaseStartUs = 0;
    int64_t ledEdgeUs = 0;
    uint16_t darkLevel = 0;
    uint16_t brightLevel = 0;
    uint16_t threshold = 0;
    uint16_t hysteresis = 0;

    // Detection (on the level with the 0.5 ms average delay removed from its time)
    bool isBright = false;  // Confirmed side of the threshold
    int64_t crossingUs = 0; // When the level last crossed the threshold towards the other side
    uint16_t previousLevel = 0;
    bool hasEdgeEvent = false; // The confirmed side changed; edgeEventUs is the crossing time
    int64_t edgeEventUs = 0;

    // Cycles
    CyclePhase cyclePhase = CyclePhase::HOLD;
    int64_t holdEndUs = 0;
    int64_t timeoutUs = 0;
    uint16_t consecutiveMissCount = 0;
    uint32_t runningMedianUs = 0;

    // Samples and statistics, published as they change
    LatencySample samples[LatencyStatus::MAX_SAMPLES];
    uint16_t sampleCount = 0;
    uint16_t targetCount = 0;
    uint16_t missCount = 0;
    LatencyStats stats;

    /**
     * Task entry point.
     *
     * @param context The LatencyService
     */
    static void taskMain(void* context);

    /**
     * Runs commands and processes frames, forever.
     */
    void run();

    /**
     * Carries out a command from the queue.
     *
     * @param command The command
     */
    void handleCommand(Command command);

    /**
     * Starts a run of the given kind: sensor on, LED off, learning from the dark level.
     *
     * @param runKind Measurement or self-test
     */
    void startRun(LatencyStatus::Kind runKind);

    /**
     * Ends a run: LED off, statistics, back to aiming with the result.
     *
     * @param isFailed True if it failed; message says why
     * @param message What to show, already formatted
     */
    void endRun(bool isFailed, const char* message);

    /**
     * Feeds one frame of samples through the average, the level, the history and the detector.
     *
     * @param samples Raw counts, oldest first
     * @param lastSampleUs Time of the frame's last sample
     */
    void processFrame(const uint16_t* samples, int64_t lastSampleUs);

    /**
     * Updates the detector with one averaged level.
     *
     * @param timeUs The level's time
     * @param value The level
     */
    void detect(int64_t timeUs, uint16_t value);

    /**
     * Advances the learning cycle on the time now.
     *
     * @param nowUs The time now
     */
    void stepLearning(int64_t nowUs);

    /**
     * Advances the measurement cycle on the time now and any detector event.
     *
     * @param nowUs The time now
     */
    void stepMeasuring(int64_t nowUs);

    /**
     * Switches the LED and remembers the time of the edge.
     *
     * @param isOn True to light it
     * @return The time of the edge
     */
    int64_t switchLed(bool isOn);

    /**
     * Returns the mean and the range of the history's levels from a time on.
     *
     * @param fromUs Oldest time to include
     * @param mean Receives the mean
     * @param range Receives max minus min
     * @return Number of entries used
     */
    size_t getHistoryStats(int64_t fromUs, uint16_t& mean, uint16_t& range);

    /**
     * Records a sample, updates the statistics and logs it.
     *
     * @param latencyUs The latency
     */
    void addSample(uint32_t latencyUs);

    /**
     * Recomputes the statistics from the samples.
     */
    void updateStats();

    /**
     * Returns the next hold: the larger of the minimum and the running median, plus a random part (plan D4).
     *
     * @return Hold in µs
     */
    uint32_t getHoldUs() const;

    /**
     * Returns how long to wait for the screen before counting a miss (plan D4).
     *
     * @return Timeout in µs
     */
    uint32_t getTimeoutUs() const;

    /**
     * Copies the task state into the published status.
     *
     * @param message New message, or nullptr to keep the current one
     * @param isVersionChanged True to bump the version (samples, state, result, message)
     */
    void publish(const char* message, bool isVersionChanged);

    /**
     * Publishes the live level, and logs it if asked, at most every LEVEL_PUBLISH_MS.
     *
     * @param nowUs The time now
     */
    void publishLevel(int64_t nowUs);

    // Constants
    static constexpr uint32_t TASK_STACK_BYTES = 6144;
    static constexpr UBaseType_t TASK_PRIORITY = 4; // Above the UI, below the motor task (5)
    static constexpr UBaseType_t COMMAND_QUEUE_LENGTH = 8;
    static constexpr uint32_t IDLE_WAIT_MS = 50;     // How long an idle task waits for a command
    static constexpr uint32_t FRAME_TIMEOUT_MS = 50; // Frames come every 12.8 ms
    static constexpr size_t AVERAGE_SAMPLES = 20;    // 1 ms at 20 kHz (plan D6)
    static constexpr int64_t SAMPLE_PERIOD_US = 1000000 / LightSensor::SAMPLE_RATE_HZ;
    static constexpr int64_t AVERAGE_DELAY_US = (AVERAGE_SAMPLES - 1) * SAMPLE_PERIOD_US / 2; // The average's centre
    static constexpr uint32_t HISTORY_DECIMATION = 20;                                        // One history entry per 1 ms
    static constexpr uint32_t LEVEL_PUBLISH_MS = 100;
    static constexpr uint16_t FULL_SCALE = 4095;

    // Learning (plan D5; starting values for the bench)
    static constexpr uint32_t LEARN_DARK_MS = 600;        // LED off this long before the dark level is taken
    static constexpr uint32_t LEARN_WINDOW_MS = 300;      // Levels are means over this long
    static constexpr uint32_t LEARN_TIMEOUT_MS = 3000;    // Longest wait for the screen to brighten and settle
    static constexpr uint32_t STEADY_BAND_PER_MILLE = 20; // Settled: the range over the window is under this
    static constexpr uint32_t MIN_CONTRAST_PER_MILLE = 50;
    static constexpr uint32_t HYSTERESIS_PERCENT = 15;
    static constexpr uint32_t RIPPLE_WINDOW_MS = 100;

    // Cycles (plan D4)
    static constexpr uint32_t MIN_HOLD_US = 200000;
    static constexpr uint32_t RANDOM_HOLD_US = 100000; // At least a frame period at 10 fps
    // Self-test: LED and sensor settle in 1-2 ms and there are no camera frames to spread over. Steps happen at frame
    // boundaries (12.8 ms), which add about 19 ms per half cycle, so 200 samples take about 10 s
    static constexpr uint32_t SELF_TEST_HOLD_US = 5000;
    static constexpr uint32_t SELF_TEST_RANDOM_HOLD_US = 5000;
    static constexpr uint32_t MIN_TIMEOUT_US = 3000000;
    static constexpr uint32_t TIMEOUT_MEDIANS = 5;
    static constexpr uint16_t MAX_CONSECUTIVE_MISSES = 3;
};
