#include "LatencyService.hpp"

#include <algorithm>
#include <cinttypes>
#include <cstdio>

#include "BoardConfig.hpp"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"

static const char* TAG = "Latency";

LatencyService::LatencyService(LightSensor& sensor, gpio_num_t pinLed)
    : sensor(sensor)
    , pinLed(pinLed) {
    static_assert(sizeof(averageWindow) / sizeof(averageWindow[0]) == AVERAGE_SAMPLES, "averageWindow holds 1 ms");
}

bool LatencyService::begin() {
    // Power the LED pin's IO domain: without LDO 4 the pin stays at 0 V whatever its level
    esp_ldo_channel_config_t ldoConfig = {};
    ldoConfig.chan_id = BoardConfig::LDO_CHANNEL_HEADER_IO;
    ldoConfig.voltage_mv = BoardConfig::LDO_HEADER_IO_VOLTAGE_MV;
    esp_err_t ldoResult = esp_ldo_acquire_channel(&ldoConfig, &ledPowerChannel);

    if (ldoResult != ESP_OK) {
        ESP_LOGE(TAG, "LDO %d for the LED pin: %s; the LED will not light", ldoConfig.chan_id, esp_err_to_name(ldoResult));
    }

    // The LED off from the start; it lights only when asked
    gpio_config_t ledConfig = {};
    ledConfig.pin_bit_mask = 1ULL << pinLed;
    ledConfig.mode = GPIO_MODE_OUTPUT;
    gpio_config(&ledConfig);
    gpio_set_level(pinLed, 0);

    commandQueue = xQueueCreate(COMMAND_QUEUE_LENGTH, sizeof(Command));

    if (commandQueue == nullptr) {
        return false;
    }

    return xTaskCreate(taskMain, "latency", TASK_STACK_BYTES, this, TASK_PRIORITY, nullptr) == pdPASS;
}

// Commands

void LatencyService::requestAim() {
    Command command = Command::AIM;
    xQueueSend(commandQueue, &command, 0);
}

void LatencyService::requestMeasurement() {
    Command command = Command::MEASURE;
    xQueueSend(commandQueue, &command, 0);
}

void LatencyService::requestSelfTest() {
    Command command = Command::SELF_TEST;
    xQueueSend(commandQueue, &command, 0);
}

void LatencyService::requestStop() {
    Command command = Command::STOP;
    xQueueSend(commandQueue, &command, 0);
}

void LatencyService::requestIdle() {
    Command command = Command::IDLE;
    xQueueSend(commandQueue, &command, 0);
}

void LatencyService::setLedOn(bool isOn) {
    Command command = isOn ? Command::LED_ON : Command::LED_OFF;
    xQueueSend(commandQueue, &command, 0);
}

void LatencyService::requestDemo() {
    Command command = Command::DEMO;
    xQueueSend(commandQueue, &command, 0);
}

void LatencyService::setLevelLogged(bool isLogged) {
    isLevelLogged = isLogged;
}

LatencyStatus LatencyService::getStatus() {
    std::lock_guard<std::mutex> lock(statusMutex);

    return status;
}

// Task

void LatencyService::taskMain(void* context) {
    static_cast<LatencyService*>(context)->run();
}

void LatencyService::run() {
    static uint16_t frame[LightSensor::FRAME_SAMPLES]; // This task only

    while (true) {
        // Commands first; while idle, waiting for one is the task's whole job
        Command command;
        TickType_t wait = state == LatencyStatus::State::IDLE ? pdMS_TO_TICKS(IDLE_WAIT_MS) : 0;

        while (xQueueReceive(commandQueue, &command, wait) == pdTRUE) {
            handleCommand(command);
            wait = 0;
        }

        if (state == LatencyStatus::State::IDLE) {
            continue;
        }

        // Without the sensor (a demo, or a start that failed) there are no frames to wait on: don't spin
        if (!sensor.isRunning()) {
            vTaskDelay(pdMS_TO_TICKS(IDLE_WAIT_MS));

            continue;
        }

        int64_t frameUs = 0;

        if (sensor.readFrame(frame, frameUs, FRAME_TIMEOUT_MS)) {
            processFrame(frame, frameUs);
        }

        // Time-driven steps run on the clock, not on the frames' times, which lag by up to a frame
        int64_t nowUs = esp_timer_get_time();

        if (state == LatencyStatus::State::LEARNING) {
            stepLearning(nowUs);
        } else if (state == LatencyStatus::State::MEASURING) {
            stepMeasuring(nowUs);
        }

        publishLevel(nowUs);
    }
}

void LatencyService::handleCommand(Command command) {
    bool isRun = state == LatencyStatus::State::LEARNING || state == LatencyStatus::State::MEASURING;

    switch (command) {
        case Command::AIM: {
            if (isRun) {
                break;
            }

            esp_err_t result = sensor.start();

            if (result != ESP_OK) {
                char message[96];
                snprintf(message, sizeof(message), "The light sensor didn't start: %s", esp_err_to_name(result));
                state = LatencyStatus::State::IDLE;
                publish(message, true);

                break;
            }

            state = LatencyStatus::State::AIMING;
            publish(nullptr, true);

            break;
        }

        case Command::MEASURE:
        case Command::SELF_TEST:
            if (!isRun) {
                startRun(command == Command::SELF_TEST ? LatencyStatus::Kind::SELF_TEST : LatencyStatus::Kind::MEASUREMENT);
            }

            break;

        case Command::STOP:
            if (isRun) {
                char message[96];
                snprintf(message, sizeof(message), "Stopped after %u sample%s", sampleCount, sampleCount == 1 ? "" : "s");
                endRun(false, message);
            }

            break;

        case Command::IDLE:
            if (isRun) {
                endRun(false, "Stopped: the screen was left");
            }

            switchLed(false);
            sensor.stop();
            state = LatencyStatus::State::IDLE;
            publish(nullptr, true);

            break;

        case Command::DEMO: {
            if (isRun) {
                break;
            }

            // A run in progress with frame-quantised samples around 120 ms: a 30 fps camera and some network spread
            sensor.stop();
            kind = LatencyStatus::Kind::MEASUREMENT;
            targetCount = DEFAULT_SAMPLE_COUNT;
            sampleCount = 0;
            missCount = 1;

            for (uint16_t index = 0; index < 40; index++) {
                uint32_t frames = esp_random() % 3;
                uint32_t latencyUs = 105000 + frames * 33333 + esp_random() % 6000;
                samples[sampleCount++] = {latencyUs};
            }

            updateStats();

            {
                std::lock_guard<std::mutex> lock(statusMutex);
                status.hasLevels = true;
                status.darkLevel = 600;
                status.brightLevel = 2900;
                status.threshold = 1750;
                status.hysteresis = 345;
                status.contrastPerMille = 561;
                status.hasResult = false;
                status.isFailed = false;
            }

            state = LatencyStatus::State::MEASURING;
            publish("Demo: synthetic samples, no sensor. STOP shows the result", true);

            break;
        }

        case Command::LED_ON:
        case Command::LED_OFF:
            // The run owns the LED
            if (!isRun) {
                switchLed(command == Command::LED_ON);
                publish(nullptr, true);
            }

            break;
    }
}

void LatencyService::startRun(LatencyStatus::Kind runKind) {
    esp_err_t result = sensor.start();

    if (result != ESP_OK) {
        char message[96];
        snprintf(message, sizeof(message), "The light sensor didn't start: %s", esp_err_to_name(result));
        state = LatencyStatus::State::IDLE;
        publish(message, true);

        return;
    }

    kind = runKind;
    targetCount = runKind == LatencyStatus::Kind::SELF_TEST ? SELF_TEST_SAMPLE_COUNT : DEFAULT_SAMPLE_COUNT;
    sampleCount = 0;
    missCount = 0;
    consecutiveMissCount = 0;
    stats = LatencyStats {};
    historyCount = 0;
    historyDecimation = 0;
    darkLevel = 0;
    brightLevel = 0;
    threshold = 0;
    hysteresis = 0;

    {
        std::lock_guard<std::mutex> lock(statusMutex);
        status.hasLevels = false;
        status.hasResult = false;
        status.isFailed = false;
        status.isSelfTestPassed = false;
        status.isRippleHigh = false;
    }

    // Learning starts dark (plan D5)
    learnPhase = LearnPhase::DARK;
    phaseStartUs = switchLed(false);
    state = LatencyStatus::State::LEARNING;
    publish(runKind == LatencyStatus::Kind::SELF_TEST ? "Self-test: learning the dark level" : "Learning the dark level", true);
    ESP_LOGI(TAG, "%s started: %u samples", runKind == LatencyStatus::Kind::SELF_TEST ? "Self-test" : "Measurement", targetCount);
}

void LatencyService::endRun(bool isFailed, const char* message) {
    switchLed(false);
    updateStats();
    state = LatencyStatus::State::AIMING;

    bool isSelfTest = kind == LatencyStatus::Kind::SELF_TEST;
    bool isPassed = isSelfTest && !isFailed && stats.count > 0 && stats.medianUs < SELF_TEST_PASS_US;

    {
        std::lock_guard<std::mutex> lock(statusMutex);
        status.hasResult = sampleCount > 0;
        status.isFailed = isFailed;
        status.isSelfTestPassed = isPassed;
    }

    publish(message, true);

    if (sampleCount > 0) {
        ESP_LOGI(TAG,
                 "%s: %u samples, median %.2f ms, p95 %.2f ms, min %.2f, max %.2f, p5 %.2f; %u missed",
                 isSelfTest ? "Self-test" : "Measurement",
                 stats.count,
                 stats.medianUs / 1000.0,
                 stats.p95Us / 1000.0,
                 stats.minUs / 1000.0,
                 stats.maxUs / 1000.0,
                 stats.p5Us / 1000.0,
                 missCount);
    }

    ESP_LOGI(TAG, "%s", message);
}

// Samples

void LatencyService::processFrame(const uint16_t* frame, int64_t lastSampleUs) {
    for (size_t index = 0; index < LightSensor::FRAME_SAMPLES; index++) {
        // Brightness: the sensor's output rises with light (measured 2026-11-02)
        uint16_t brightness = std::min<uint16_t>(frame[index], FULL_SCALE);

        averageSum -= averageWindow[averageIndex];
        averageWindow[averageIndex] = brightness;
        averageSum += brightness;
        averageIndex = (averageIndex + 1) % AVERAGE_SAMPLES;

        if (averageFill < AVERAGE_SAMPLES) {
            averageFill++;

            continue;
        }

        // The average stands for the middle of its window: half a millisecond back from the newest sample (plan D6)
        uint16_t value = static_cast<uint16_t>(averageSum / AVERAGE_SAMPLES);
        int64_t sampleUs = lastSampleUs - static_cast<int64_t>(LightSensor::FRAME_SAMPLES - 1 - index) * SAMPLE_PERIOD_US;
        int64_t valueUs = sampleUs - AVERAGE_DELAY_US;

        level = value;
        levelMin = std::min(levelMin, value);
        levelMax = std::max(levelMax, value);

        // History at 1 kHz for the learning cycle
        if (++historyDecimation >= HISTORY_DECIMATION) {
            historyDecimation = 0;
            size_t slot = historyCount % HISTORY_LENGTH;
            historyTimeUs[slot] = valueUs;
            historyLevel[slot] = value;
            historyCount++;
        }

        if (state == LatencyStatus::State::MEASURING) {
            detect(valueUs, value);
        }
    }
}

void LatencyService::detect(int64_t timeUs, uint16_t value) {
    // The crossing time is when the level passed the threshold; the hysteresis only confirms it (plan D5)
    bool isCrossingUp = previousLevel <= threshold && value > threshold;
    bool isCrossingDown = previousLevel >= threshold && value < threshold;
    previousLevel = value;

    if (!isBright && isCrossingUp) {
        crossingUs = timeUs;
    } else if (isBright && isCrossingDown) {
        crossingUs = timeUs;
    }

    uint16_t half = hysteresis / 2;

    if (!isBright && value > threshold + half) {
        isBright = true;
        hasEdgeEvent = true;
        edgeEventUs = crossingUs;
    } else if (isBright && value + half < threshold) {
        isBright = false;
        hasEdgeEvent = true;
        edgeEventUs = crossingUs;
    }
}

size_t LatencyService::getHistoryStats(int64_t fromUs, uint16_t& mean, uint16_t& range) {
    size_t available = std::min(historyCount, HISTORY_LENGTH);
    uint32_t sum = 0;
    uint16_t lowest = UINT16_MAX;
    uint16_t highest = 0;
    size_t used = 0;

    // Newest first, back to fromUs
    for (size_t back = 1; back <= available; back++) {
        size_t slot = (historyCount - back) % HISTORY_LENGTH;

        if (historyTimeUs[slot] < fromUs) {
            break;
        }

        sum += historyLevel[slot];
        lowest = std::min(lowest, historyLevel[slot]);
        highest = std::max(highest, historyLevel[slot]);
        used++;
    }

    mean = used > 0 ? static_cast<uint16_t>(sum / used) : 0;
    range = used > 0 ? static_cast<uint16_t>(highest - lowest) : 0;

    return used;
}

// Learning (plan D5)

void LatencyService::stepLearning(int64_t nowUs) {
    static constexpr int64_t WINDOW_US = static_cast<int64_t>(LEARN_WINDOW_MS) * 1000;
    static constexpr uint32_t STEADY_BAND = FULL_SCALE * STEADY_BAND_PER_MILLE / 1000;
    static constexpr uint32_t MIN_SWING = FULL_SCALE * MIN_CONTRAST_PER_MILLE / 1000;

    if (learnPhase == LearnPhase::DARK) {
        if (nowUs - phaseStartUs < static_cast<int64_t>(LEARN_DARK_MS) * 1000) {
            return;
        }

        uint16_t range = 0;

        if (getHistoryStats(nowUs - WINDOW_US, darkLevel, range) == 0) {
            endRun(true, "The light sensor sends nothing: is it plugged in?");

            return;
        }

        learnPhase = LearnPhase::BRIGHT;
        ledEdgeUs = switchLed(true);
        phaseStartUs = ledEdgeUs;
        publish(kind == LatencyStatus::Kind::SELF_TEST ? "Self-test: waiting for the LED" : "Waiting for the screen to brighten", true);
        ESP_LOGI(TAG, "Dark level %u (range %u)", darkLevel, range);

        return;
    }

    // Bright: settled after a change, or out of time
    uint16_t mean = 0;
    uint16_t range = 0;
    bool isWindowFull = nowUs - ledEdgeUs >= WINDOW_US;
    getHistoryStats(std::max(nowUs - WINDOW_US, ledEdgeUs), mean, range);
    bool hasChanged = mean > darkLevel + MIN_SWING;
    bool isSettled = isWindowFull && range < STEADY_BAND;
    bool isTimedOut = nowUs - ledEdgeUs > static_cast<int64_t>(LEARN_TIMEOUT_MS) * 1000;

    if (!(hasChanged && isSettled) && !isTimedOut) {
        return;
    }

    brightLevel = mean;
    uint32_t swing = brightLevel > darkLevel ? brightLevel - darkLevel : 0;
    uint32_t contrastPerMille = swing * 1000 / FULL_SCALE;

    if (swing < MIN_SWING) {
        char message[112];
        const char* advice = kind == LatencyStatus::Kind::SELF_TEST ? "Press the sensor flat against the LED" : "LED closer to the lens, sensor on a brighter patch";
        snprintf(message,
                 sizeof(message),
                 "Not enough contrast: %" PRIu32 ".%" PRIu32 " %% of the range, needs %" PRIu32 " %%. %s",
                 contrastPerMille / 10,
                 contrastPerMille % 10,
                 MIN_CONTRAST_PER_MILLE / 10,
                 advice);
        ESP_LOGW(TAG, "Bright level %u, dark %u: swing %" PRIu32, brightLevel, darkLevel, swing);
        endRun(true, message);

        return;
    }

    threshold = static_cast<uint16_t>(darkLevel + swing / 2);
    hysteresis = static_cast<uint16_t>(swing * HYSTERESIS_PERCENT / 100);

    // Ripple: the 1 ms average's range over the last 100 ms while bright (backlight PWM, lamp flicker)
    uint16_t rippleMean = 0;
    uint16_t ripple = 0;
    getHistoryStats(nowUs - static_cast<int64_t>(RIPPLE_WINDOW_MS) * 1000, rippleMean, ripple);

    // The first estimate: the first history entry past the threshold after the edge; the sample itself is discarded
    runningMedianUs = 0;
    size_t available = std::min(historyCount, HISTORY_LENGTH);

    for (size_t back = available; back >= 1; back--) {
        size_t slot = (historyCount - back) % HISTORY_LENGTH;

        if (historyTimeUs[slot] > ledEdgeUs && historyLevel[slot] > threshold) {
            runningMedianUs = static_cast<uint32_t>(historyTimeUs[slot] - ledEdgeUs);
            break;
        }
    }

    {
        std::lock_guard<std::mutex> lock(statusMutex);
        status.hasLevels = true;
        status.darkLevel = darkLevel;
        status.brightLevel = brightLevel;
        status.threshold = threshold;
        status.hysteresis = hysteresis;
        status.contrastPerMille = static_cast<uint16_t>(contrastPerMille);
        status.ripple = ripple;
        status.isRippleHigh = ripple > hysteresis;
    }

    ESP_LOGI(TAG,
             "Learned: dark %u, bright %u, threshold %u, hysteresis %u, contrast %" PRIu32 " per mille, ripple %u%s, first estimate %.1f ms",
             darkLevel,
             brightLevel,
             threshold,
             hysteresis,
             contrastPerMille,
             ripple,
             ripple > hysteresis ? " (above the hysteresis)" : "",
             runningMedianUs / 1000.0);

    // Measuring: the screen is bright and the LED on; the first cycle turns it off after a hold
    isBright = true;
    previousLevel = brightLevel;
    hasEdgeEvent = false;
    cyclePhase = CyclePhase::HOLD;
    holdEndUs = nowUs + getHoldUs();
    state = LatencyStatus::State::MEASURING;

    if (ripple > hysteresis) {
        publish("Measuring. Ripple on the bright level is high: detections may trigger on flicker", true);
    } else {
        publish("Measuring", true);
    }
}

// Cycles (plan D4)

void LatencyService::stepMeasuring(int64_t nowUs) {
    bool hasEvent = hasEdgeEvent;
    int64_t eventUs = edgeEventUs;
    hasEdgeEvent = false;

    if (cyclePhase == CyclePhase::HOLD) {
        if (nowUs < holdEndUs) {
            return;
        }

        ledEdgeUs = switchLed(!isLedOn);
        timeoutUs = nowUs + getTimeoutUs();
        cyclePhase = CyclePhase::WAIT_EDGE;

        return;
    }

    // Waiting: an event in the expected direction, after the LED's edge. Only LED on to bright is a sample (D-035); the
    // screen going dark is waited for so the next on edge starts from dark
    bool isExpected = hasEvent && isBright == isLedOn && eventUs > ledEdgeUs;

    if (isExpected && !isLedOn) {
        consecutiveMissCount = 0;
        cyclePhase = CyclePhase::HOLD;
        holdEndUs = nowUs + getHoldUs();

        return;
    }

    if (isExpected) {
        consecutiveMissCount = 0;
        addSample(static_cast<uint32_t>(eventUs - ledEdgeUs));

        if (sampleCount >= targetCount) {
            char message[112];

            if (kind == LatencyStatus::Kind::SELF_TEST) {
                bool isPassed = stats.medianUs < SELF_TEST_PASS_US;
                snprintf(message, sizeof(message), "Self-test %s: %.2f ms median, %.2f ms p95", isPassed ? "passed" : "failed", stats.medianUs / 1000.0, stats.p95Us / 1000.0);
            } else {
                const char* format = stats.p95Us < 10000 ? "Done: %.2f ms median, %.2f ms p95 over %u samples" : "Done: %.1f ms median, %.1f ms p95 over %u samples";
                snprintf(message, sizeof(message), format, stats.medianUs / 1000.0, stats.p95Us / 1000.0, sampleCount);
            }

            endRun(false, message);

            return;
        }

        cyclePhase = CyclePhase::HOLD;
        holdEndUs = nowUs + getHoldUs();

        return;
    }

    if (nowUs < timeoutUs) {
        return;
    }

    // A miss: start the next cycle from dark
    missCount++;
    consecutiveMissCount++;
    ESP_LOGW(TAG, "Miss %u: no change %.1f s after the LED went %s", consecutiveMissCount, getTimeoutUs() / 1000000.0, isLedOn ? "on" : "off");

    if (consecutiveMissCount >= MAX_CONSECUTIVE_MISSES) {
        endRun(true, "No change seen three times in a row: did the sensor or the LED move?");

        return;
    }

    switchLed(false);
    cyclePhase = CyclePhase::HOLD;
    holdEndUs = nowUs + getHoldUs();
    publish(nullptr, true);
}

int64_t LatencyService::switchLed(bool isOn) {
    gpio_set_level(pinLed, isOn ? 1 : 0);
    isLedOn = isOn;

    return esp_timer_get_time();
}

void LatencyService::addSample(uint32_t latencyUs) {
    if (sampleCount < LatencyStatus::MAX_SAMPLES) {
        samples[sampleCount] = {latencyUs};
        sampleCount++;
    }

    updateStats();
    runningMedianUs = stats.medianUs;
    ESP_LOGI(TAG, "Sample %u: %.2f ms", sampleCount, latencyUs / 1000.0);
    publish(nullptr, true);
}

void LatencyService::updateStats() {
    static uint32_t all[LatencyStatus::MAX_SAMPLES]; // This task only

    for (size_t index = 0; index < sampleCount; index++) {
        all[index] = samples[index].latencyUs;
    }

    stats = LatencyStats {};
    stats.count = sampleCount;

    if (sampleCount == 0) {
        return;
    }

    std::sort(all, all + sampleCount);

    // Nearest rank: the value at ceil(p × n) - 1
    auto percentile = [](const uint32_t* sorted, size_t count, uint32_t percent) {
        size_t rank = (count * percent + 99) / 100;

        return sorted[rank > 0 ? rank - 1 : 0];
    };

    stats.minUs = all[0];
    stats.maxUs = all[sampleCount - 1];
    stats.medianUs = percentile(all, sampleCount, 50);
    stats.p5Us = percentile(all, sampleCount, 5);
    stats.p95Us = percentile(all, sampleCount, 95);
}

uint32_t LatencyService::getHoldUs() const {
    // The self-test has only the LED and the sensor to settle
    if (kind == LatencyStatus::Kind::SELF_TEST) {
        return SELF_TEST_HOLD_US + esp_random() % SELF_TEST_RANDOM_HOLD_US;
    }

    return std::max(MIN_HOLD_US, runningMedianUs) + esp_random() % RANDOM_HOLD_US;
}

uint32_t LatencyService::getTimeoutUs() const {
    return std::max(MIN_TIMEOUT_US, runningMedianUs * TIMEOUT_MEDIANS);
}

// Publishing

void LatencyService::publish(const char* message, bool isVersionChanged) {
    std::lock_guard<std::mutex> lock(statusMutex);
    status.state = state;
    status.kind = kind;
    status.isLedOn = isLedOn;
    status.targetCount = targetCount;
    status.sampleCount = sampleCount;
    status.missCount = missCount;
    std::copy(samples, samples + sampleCount, status.samples);
    status.stats = stats;

    if (message != nullptr) {
        snprintf(status.message, sizeof(status.message), "%s", message);
    }

    if (isVersionChanged) {
        status.version++;
    }
}

void LatencyService::publishLevel(int64_t nowUs) {
    if (nowUs - levelPublishedUs < static_cast<int64_t>(LEVEL_PUBLISH_MS) * 1000) {
        return;
    }

    levelPublishedUs = nowUs;
    uint16_t lowest = levelMin == UINT16_MAX ? level : levelMin;

    {
        std::lock_guard<std::mutex> lock(statusMutex);
        status.level = level;
        status.levelMin = lowest;
        status.levelMax = levelMax;
    }

    if (isLevelLogged) {
        ESP_LOGI(TAG,
                 "Level %4u (min %4u, max %4u, ripple %3u), brightness 0-4095, %" PRIu32 " frames dropped",
                 level,
                 lowest,
                 levelMax,
                 static_cast<uint16_t>(levelMax - lowest),
                 sensor.getDroppedFrameCount());
    }

    levelMin = UINT16_MAX;
    levelMax = 0;
}
