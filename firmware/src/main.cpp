#include <Adafruit_VL53L0X.h>
#include <Arduino.h>

#include <cstring>

#include "SerialConsole.hpp"
#include "St3215.hpp"
#include "SwUartPioBus.hpp"
#include "stream_operators.hpp"

// Single GPIO shared with the servo bus, through a ~1k series resistor.
static constexpr uint kServoPin = 23;

// STS3215 servos ship at 1 Mbps.
static constexpr uint kServoBaud = 1000000;

// Highest ID probed by the "scan" command.
static constexpr uint8_t kMaxScanId = 20;

// Most servos a single "sync"/"syncread" command can address.
static constexpr size_t kMaxSyncServos = 8;

// Servo rocked back and forth by the "tilt" test.
static constexpr uint8_t kTiltServoId = 3;

// How far the "tilt" test swings either side of centre, in degrees.
static constexpr float kTiltAngleDegrees = 4.0f;

// How long the "tilt" test holds each side before swinging back.
static constexpr unsigned long kTiltIntervalMs = 1000;

static SwUartPioBus bus(kServoPin, kServoBaud);
static St3215 servo(bus);
static SerialConsole console(Serial);

Adafruit_VL53L0X distanceSensor;
bool hasDistanceSensor = false;

// Servos discovered by the last scan; used by "stop" so no motor is missed.
static uint8_t discoveredIds[kMaxScanId];
static uint8_t discoveredCount = 0;

// Print each new distance measurement; toggled by the "debug" command.
static bool isDebugEnabled = false;

// Rock the beam between two positions; toggled by the "tilt" command.
static bool isTiltEnabled = false;
static unsigned long lastTiltTimeMs = 0;
static bool isTiltHigh = true;

/** Prints a deci-volt value (tenths of a volt) as "X.Y V". */
static void printVolts(int deciVolts) {
    Serial << (deciVolts / 10) << "." << (deciVolts % 10) << " V";
}

/** Prints the servo's decoded status/fault flags. */
static void printStatusFlags(const St3215::ServoStatus& status) {
    Serial << "Status flags: ";

    if (status.raw == 0) {
        Serial << "none";
    } else {
        if (status.hasVoltageFault) {
            Serial << "voltage ";
        }

        if (status.hasSensorFault) {
            Serial << "sensor ";
        }

        if (status.hasTemperatureFault) {
            Serial << "temperature ";
        }

        if (status.hasCurrentFault) {
            Serial << "current ";
        }

        if (status.hasOverloadFault) {
            Serial << "overload ";
        }
    }

    Serial << endl;
}

/** Reads and prints a servo's identification, limits, and live feedback. */
static void printServoInfo(uint8_t id) {
    St3215::ServoInfo info;

    if (!servo.readInfo(id, info)) {
        Serial << "servo " << id << " no response" << endl;

        return;
    }

    Serial << "Firmware version: " << info.firmwareMajor << "." << info.firmwareMinor << endl;
    Serial << "Servo series version: " << info.modelMajor << "." << info.modelMinor << endl;
    Serial << "Stored ID: " << info.id << ", baud index: " << info.baudIndex << endl;
    Serial << "Voltage limits: ";
    printVolts(info.minVoltageDeciV);
    Serial << " - ";
    printVolts(info.maxVoltageDeciV);
    Serial << endl;
    Serial << "Max temperature: " << info.maxTemperatureC << " C" << endl;
    Serial << "Position PID: kp=" << info.positionKp << " kd=" << info.positionKd << " ki=" << info.positionKi << endl;

    St3215::ServoFeedback feedback;

    if (servo.readFeedback(id, feedback)) {
        Serial << "Voltage: ";
        printVolts(feedback.voltageDeciV);
        Serial << endl;
        Serial << "Temperature: " << feedback.temperatureC << " C" << endl;
        Serial << "Position: " << feedback.position << endl;
        printStatusFlags(feedback.status);
    }
}

/** Reads and prints a servo's live feedback. */
static void printFeedback(uint8_t id) {
    St3215::ServoFeedback feedback;

    if (!servo.readFeedback(id, feedback)) {
        Serial << "servo " << id << " no response" << endl;

        return;
    }

    Serial << "pos=" << feedback.position << " speed=" << feedback.speed << " load=" << feedback.load << " current=" << feedback.currentMa << "mA moving=" << feedback.isMoving << endl;
    Serial << "Voltage: ";
    printVolts(feedback.voltageDeciV);
    Serial << " temp=" << feedback.temperatureC << " C" << endl;
    printStatusFlags(feedback.status);
}

/** Pings every ID up to kMaxScanId and lists the ones that respond. */
static void scanBus() {
    Serial << "Scanning IDs 1.." << kMaxScanId << " ..." << endl;

    discoveredCount = 0;

    for (uint8_t id = 1; id <= kMaxScanId; id++) {
        if (servo.ping(id) != -1) {
            Serial << "  found servo " << id << endl;
            discoveredIds[discoveredCount++] = id;
        }
    }

    Serial << discoveredCount << " servo(s) found" << endl;
}

/**
 * Parses an "on"/"off" (or "1"/"0") argument into a flag.
 *
 * @param c      Console to pull the token from.
 * @param target Flag written when the token is recognised.
 * @returns True when a valid token was given, false otherwise.
 */
static bool parseOnOff(SerialConsole& c, bool& target) {
    const char* token = c.nextToken();

    if (token == nullptr) {
        return false;
    }

    if (strcmp(token, "on") == 0 || strcmp(token, "1") == 0) {
        target = true;

        return true;
    }

    if (strcmp(token, "off") == 0 || strcmp(token, "0") == 0) {
        target = false;

        return true;
    }

    return false;
}

/** Registers all servo console commands. */
static void registerCommands() {
    console.addCommand("scan", "scan                       - find servos (IDs 1..20)", [](SerialConsole&) {
        scanBus();
    });

    console.addCommand("ping", "ping <id>                  - ping a servo", [](SerialConsole& c) {
        const int id = c.nextInt(-1);

        if (servo.ping(id) != -1) {
            Serial << "servo " << id << " OK" << endl;
        } else {
            Serial << "servo " << id << " no response" << endl;
        }
    });

    console.addCommand("info", "info <id>                  - version, limits, live feedback", [](SerialConsole& c) {
        printServoInfo((uint8_t)c.nextInt(-1));
    });

    console.addCommand("feedback", "feedback <id>              - live position/speed/load/etc", [](SerialConsole& c) {
        printFeedback((uint8_t)c.nextInt(-1));
    });

    console.addCommand("id", "id <from> <to>             - change ID (ONLY ONE servo on bus!)", [](SerialConsole& c) {
        const int from = c.nextInt(-1);
        const int to = c.nextInt(-1);

        if (from < 1 || from > 253 || to < 1 || to > 253) {
            Serial << "usage: id <from 1..253> <to 1..253>" << endl;

            return;
        }

        if (servo.setId((uint8_t)from, (uint8_t)to)) {
            Serial << "ID changed " << from << " -> " << to << endl;
        } else {
            Serial << "ID change failed (is exactly one servo connected?)" << endl;
        }
    });

    console.addCommand("move", "move <id> <pos> [spd] [acc] - position 0..4095 (multi-turn in step mode)", [](SerialConsole& c) {
        const int id = c.nextInt(-1);
        const int pos = c.nextInt(-100000);
        const int speed = c.nextInt(0);
        const int acc = c.nextInt(0);

        if (id < 1 || pos < -32767 || pos > 32767) {
            Serial << "usage: move <id> <pos -32767..32767> [speed] [acc]" << endl;

            return;
        }

        servo.writePos((uint8_t)id, (int16_t)pos, (uint16_t)speed, (uint8_t)acc);
        Serial << "move " << id << " -> " << pos << endl;
    });

    console.addCommand("sync", "sync <pos> <id> [id...]    - move several servos together", [](SerialConsole& c) {
        const int pos = c.nextInt(-1);

        if (pos < 0 || pos > 4095) {
            Serial << "usage: sync <pos 0..4095> <id> [id...]" << endl;

            return;
        }

        uint8_t ids[kMaxSyncServos];
        int16_t positions[kMaxSyncServos];
        uint16_t speeds[kMaxSyncServos];
        uint8_t accs[kMaxSyncServos];
        uint8_t count = 0;

        for (int id = c.nextInt(-1); id > 0 && count < kMaxSyncServos; id = c.nextInt(-1)) {
            ids[count] = (uint8_t)id;
            positions[count] = (int16_t)pos;
            speeds[count] = 0;
            accs[count] = 0;
            count++;
        }

        if (count == 0) {
            Serial << "usage: sync <pos 0..4095> <id> [id...]" << endl;

            return;
        }

        servo.syncWritePos(ids, count, positions, speeds, accs);
        Serial << "sync " << count << " servo(s) -> " << pos << endl;
    });

    console.addCommand("syncread", "syncread <id> [id...]      - read feedback from several at once", [](SerialConsole& c) {
        uint8_t ids[kMaxSyncServos];
        uint8_t count = 0;

        for (int id = c.nextInt(-1); id > 0 && count < kMaxSyncServos; id = c.nextInt(-1)) {
            ids[count++] = (uint8_t)id;
        }

        if (count == 0) {
            Serial << "usage: syncread <id> [id...]" << endl;

            return;
        }

        St3215::ServoFeedback feedback[kMaxSyncServos];
        servo.syncReadFeedback(ids, count, feedback);

        for (uint8_t i = 0; i < count; i++) {
            Serial << "servo " << ids[i] << ": ";

            if (!feedback[i].isValid) {
                Serial << "no response" << endl;

                continue;
            }

            Serial << "pos=" << feedback[i].position << " speed=" << feedback[i].speed << " load=" << feedback[i].load << " V=";
            printVolts(feedback[i].voltageDeciV);
            Serial << endl;
        }
    });

    console.addCommand("speed", "speed <id> <val> [acc]     - continuous speed (needs mode 1)", [](SerialConsole& c) {
        const int id = c.nextInt(-1);
        const int value = c.nextInt(0);
        const int acc = c.nextInt(0);

        if (id < 1) {
            Serial << "usage: speed <id> <value> [acc]  (set mode 1 first)" << endl;

            return;
        }

        servo.writeSpeed((uint8_t)id, (int16_t)value, (uint8_t)acc);
        Serial << "speed " << id << " = " << value << endl;
    });

    console.addCommand("mode", "mode <id> <0|1|2|3>        - position/speed/pwm/step", [](SerialConsole& c) {
        const int id = c.nextInt(-1);
        const int mode = c.nextInt(-1);

        if (id < 1 || mode < 0 || mode > 3) {
            Serial << "usage: mode <id> <0=pos 1=speed 2=pwm 3=step>" << endl;

            return;
        }

        if (servo.setMode((uint8_t)id, (St3215::Mode)mode)) {
            Serial << "mode " << id << " = " << mode << endl;
        } else {
            Serial << "mode change failed" << endl;
        }
    });

    console.addCommand("calibrate", "calibrate <id>             - set current position as mid (2047)", [](SerialConsole& c) {
        const int id = c.nextInt(-1);

        if (servo.calibrateMid((uint8_t)id)) {
            Serial << "calibrated servo " << id << " mid-point" << endl;
        } else {
            Serial << "calibrate failed" << endl;
        }
    });

    console.addCommand("anglelimit", "anglelimit <id> <min> <max> - position limits (0..4095, 0/0 = none)", [](SerialConsole& c) {
        const int id = c.nextInt(-1);
        const int low = c.nextInt(-1);
        const int high = c.nextInt(-1);

        if (id < 1 || low < 0 || low > 4095 || high < 0 || high > 4095) {
            Serial << "usage: anglelimit <id> <min 0..4095> <max 0..4095>" << endl;

            return;
        }

        if (servo.setAngleLimits((uint8_t)id, (uint16_t)low, (uint16_t)high)) {
            Serial << "angle limits " << id << " = " << low << ".." << high << endl;
        } else {
            Serial << "set angle limits failed" << endl;
        }
    });

    console.addCommand("torquelimit", "torquelimit <id> <0..1000> - output torque limit", [](SerialConsole& c) {
        const int id = c.nextInt(-1);
        const int limit = c.nextInt(-1);

        if (id < 1 || limit < 0 || limit > 1000) {
            Serial << "usage: torquelimit <id> <0..1000>" << endl;

            return;
        }

        if (servo.setTorqueLimit((uint8_t)id, (uint16_t)limit)) {
            Serial << "torque limit " << id << " = " << limit << endl;
        } else {
            Serial << "set torque limit failed" << endl;
        }
    });

    console.addCommand("pid", "pid <id> <kp> <kd> <ki>    - position loop gains (0..255)", [](SerialConsole& c) {
        const int id = c.nextInt(-1);
        const int kp = c.nextInt(-1);
        const int kd = c.nextInt(-1);
        const int ki = c.nextInt(-1);

        if (id < 1 || kp < 0 || kp > 255 || kd < 0 || kd > 255 || ki < 0 || ki > 255) {
            Serial << "usage: pid <id> <kp> <kd> <ki>  (each 0..255)" << endl;

            return;
        }

        if (servo.setPid((uint8_t)id, (uint8_t)kp, (uint8_t)kd, (uint8_t)ki)) {
            Serial << "pid " << id << " = " << kp << "/" << kd << "/" << ki << endl;
        } else {
            Serial << "set pid failed" << endl;
        }
    });

    console.addCommand("unload", "unload <id> <mask>         - faults that release torque (bitmask)", [](SerialConsole& c) {
        const int id = c.nextInt(-1);
        const int mask = c.nextInt(-1);

        if (id < 1 || mask < 0 || mask > 255) {
            Serial << "usage: unload <id> <mask>  (0x01 volt 0x02 sensor 0x04 temp 0x08 current 0x20 overload)" << endl;

            return;
        }

        if (servo.setUnloadingCondition((uint8_t)id, (uint8_t)mask)) {
            Serial << "unload condition " << id << " = " << mask << endl;
        } else {
            Serial << "set unload condition failed" << endl;
        }
    });

    console.addCommand("stop", "stop [id]                  - stop & hold all known servos (or one)", [](SerialConsole& c) {
        const int id = c.nextInt(-1);

        if (id > 0) {
            if (servo.stop((uint8_t)id)) {
                Serial << "stopped servo " << id << endl;
            } else {
                Serial << "stop failed for servo " << id << endl;
            }

            return;
        }

        for (uint8_t i = 0; i < discoveredCount; i++) {
            servo.stop(discoveredIds[i]);
        }

        Serial << "stopped " << discoveredCount << " servo(s)" << endl;
    });

    console.addCommand("relax", "relax [id]                 - release torque / free-wheel (all via broadcast, or one)", [](SerialConsole& c) {
        const int id = c.nextInt(-1);

        if (id > 0) {
            servo.setTorque((uint8_t)id, false);
            Serial << "relaxed servo " << id << endl;

            return;
        }

        servo.relaxAll();
        Serial << "relaxed all servos (broadcast)" << endl;
    });

    console.addCommand("torque", "torque <id> <0|1>          - torque off / on", [](SerialConsole& c) {
        const int id = c.nextInt(-1);
        const int on = c.nextInt(-1);

        servo.setTorque((uint8_t)id, on != 0);
        Serial << "torque " << id << " = " << (on != 0) << endl;
    });

    console.addCommand("debug", "debug <on|off>             - print ball distance measurements", [](SerialConsole& c) {
        if (!parseOnOff(c, isDebugEnabled)) {
            Serial << "usage: debug <on|off>  (currently " << (isDebugEnabled ? "on" : "off") << ")" << endl;

            return;
        }

        if (isDebugEnabled && !hasDistanceSensor) {
            Serial << "debug on, but no distance sensor was detected" << endl;

            return;
        }

        Serial << "debug " << (isDebugEnabled ? "on" : "off") << endl;
    });

    console.addCommand("tilt", "tilt <on|off>              - rock the beam between two positions", [](SerialConsole& c) {
        if (!parseOnOff(c, isTiltEnabled)) {
            Serial << "usage: tilt <on|off>  (currently " << (isTiltEnabled ? "on" : "off") << ")" << endl;

            return;
        }

        if (isTiltEnabled) {
            // Swing on the next loop pass instead of waiting out the interval.
            lastTiltTimeMs = millis() - kTiltIntervalMs;
        } else {
            servo.stop(kTiltServoId);
        }

        Serial << "tilt " << (isTiltEnabled ? "on" : "off") << endl;
    });
}

int16_t angleToPosition(float angleDegrees) {
    // Convert angle in degrees to position value (0-4095)
    // Assuming 0 degrees corresponds to position 2048 (midpoint)
    // and 360 degrees corresponds to position 4095
    float position = (angleDegrees / 360.0f) * 4095.0f + 2048.0f;

    return static_cast<int16_t>(position);
}

void setup() {
    Serial.begin(115200);
    Serial.ignoreFlowControl(true);

    // Wait for the monitor, but don't block forever if running headless.
    while (!Serial && millis() < 8000) {
        delay(50);
    }

    delay(100);

    Serial << "ST3215 console (custom driver over single-wire PIO)" << endl;

    if (!servo.begin()) {
        Serial << "Failed to start PIO UART (no free state machine)" << endl;

        return;
    }

    registerCommands();
    scanBus();
    console.printHelp();

    // Set all servos to position mode and stop them
    servo.setMode(St3215::kBroadcastId, St3215::Mode::Position);
    servo.stop(St3215::kBroadcastId);

    // Set initial speeds
    // servo.writeSpeed(1, 500);
    // servo.writeSpeed(2, 1000);
    // servo.writeSpeed(3, 1500);
    // servo.writeSpeed(4, 2000);

    // Setup distance sensor
    // Increase default I2C bus speed for faster distance measurements
    // Wire1.setClock(400000);

    // Try to initialize the distance sensor with high-speed mode
    Serial << "Setting up distance sensor" << endl;

    Wire1.setSDA(14);
    Wire1.setSCL(15);
    // Wire1.setClock(400000);
    // Wire1.begin();

    if (distanceSensor.begin(VL53L0X_I2C_ADDR, false, &Wire1, Adafruit_VL53L0X::VL53L0X_SENSE_DEFAULT)) {
        hasDistanceSensor = true;

        Serial << "VL53L0X distance sensor initialized successfully" << endl;

        // Start continuous distance measurements
        distanceSensor.startRangeContinuous(30);
    } else {
        Serial << "Failed to initialize VL53L0X distance sensor" << endl;
    }
}

void loop() {
    console.loop();

    const unsigned long currentTimeMs = millis();

    // Tilt the beam back and forth between +-kTiltAngleDegrees
    if (isTiltEnabled && currentTimeMs - lastTiltTimeMs >= kTiltIntervalMs) {
        const int16_t position = angleToPosition(isTiltHigh ? kTiltAngleDegrees : -kTiltAngleDegrees);
        isTiltHigh = !isTiltHigh;

        servo.writePos(kTiltServoId, position);

        lastTiltTimeMs = currentTimeMs;
    }

    // Read distance sensor and print the result if a new measurement is available
    if (isDebugEnabled && hasDistanceSensor && distanceSensor.isRangeComplete()) {
        const uint16_t distanceMm = distanceSensor.readRangeResult();

        if (distanceSensor.timeoutOccurred()) {
            Serial << "Distance sensor timeout!" << endl;
        } else {
            Serial << "Distance: " << distanceMm << " mm" << endl;
        }
    }
}
