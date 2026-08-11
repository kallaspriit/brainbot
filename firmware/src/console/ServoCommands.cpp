#include "console/ServoCommands.hpp"

#include <Arduino.h>
#include <cstring>

#include "hardware/HardwareLock.hpp"
#include "stream_operators.hpp"

namespace {

/** Operating mode names, indexed by St3215::Mode. */
const char* const kModeNames[] = {"position", "speed", "pwm", "step"};

/** Prints a deci-volt value (tenths of a volt) as "X.Y V". */
void printVolts(int deciVolts) {
    Serial << (deciVolts / 10) << "." << (deciVolts % 10) << " V";
}

/**
 * Prints the names of the fault bits set in a mask, or "none". Shared by the
 * live status byte and the unloading-condition mask, which use the same layout.
 *
 * @param raw Fault bitmask.
 */
void printFaultNames(uint8_t raw) {
    if (raw == 0) {
        Serial << "none";

        return;
    }

    if ((raw & (uint8_t)St3215::Fault::Voltage) != 0) {
        Serial << "voltage ";
    }

    if ((raw & (uint8_t)St3215::Fault::Sensor) != 0) {
        Serial << "sensor ";
    }

    if ((raw & (uint8_t)St3215::Fault::Temperature) != 0) {
        Serial << "temperature ";
    }

    if ((raw & (uint8_t)St3215::Fault::Current) != 0) {
        Serial << "current ";
    }

    if ((raw & (uint8_t)St3215::Fault::Overload) != 0) {
        Serial << "overload ";
    }
}

/** Prints the servo's decoded status/fault flags. */
void printStatusFlags(const St3215::ServoStatus& status) {
    Serial << "Status flags: ";
    printFaultNames(status.raw);
    Serial << endl;
}

} // namespace

int ServoCommands::readByteLocked(uint8_t id, St3215::Register reg) {
    const HardwareLock lock;

    return servo_.readByte(id, reg);
}

int ServoCommands::readWordLocked(uint8_t id, St3215::Register reg) {
    const HardwareLock lock;

    return servo_.readWord(id, reg);
}

void ServoCommands::scanBus() {
    Serial << "Scanning IDs 1.." << Config::kMaxScanId << " ..." << endl;

    discoveredCount_ = 0;

    // Locked per ping rather than across the whole scan: a full sweep of absent
    // IDs is twenty reply timeouts, and the control tick should not wait them out.
    for (uint8_t id = 1; id <= Config::kMaxScanId; id++) {
        bool isPresent = false;

        {
            const HardwareLock lock;

            isPresent = servo_.ping(id) != -1;
        }

        if (isPresent) {
            Serial << "  found servo " << id << endl;
            discoveredIds_[discoveredCount_++] = id;
        }
    }

    Serial << discoveredCount_ << " servo(s) found" << endl;
}

void ServoCommands::printServoInfo(uint8_t id) {
    St3215::ServoInfo info;
    St3215::ServoFeedback feedback;
    bool hasInfo = false;

    {
        const HardwareLock lock;

        hasInfo = servo_.readInfo(id, info);

        if (hasInfo) {
            servo_.readFeedback(id, feedback);
        }
    }

    if (!hasInfo) {
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

    if (feedback.isValid) {
        Serial << "Voltage: ";
        printVolts(feedback.voltageDeciV);
        Serial << endl;
        Serial << "Temperature: " << feedback.temperatureC << " C" << endl;
        Serial << "Position: " << feedback.position << endl;
        printStatusFlags(feedback.status);
    }
}

void ServoCommands::printFeedback(uint8_t id) {
    St3215::ServoFeedback feedback;
    bool isOk = false;

    {
        const HardwareLock lock;

        isOk = servo_.readFeedback(id, feedback);
    }

    if (!isOk) {
        Serial << "servo " << id << " no response" << endl;

        return;
    }

    Serial << "pos=" << feedback.position << " speed=" << feedback.speed << " load=" << feedback.load << " current=" << feedback.currentMa << "mA moving=" << feedback.isMoving << endl;
    Serial << "Voltage: ";
    printVolts(feedback.voltageDeciV);
    Serial << " temp=" << feedback.temperatureC << " C" << endl;
    printStatusFlags(feedback.status);
}

void ServoCommands::registerCommands(SerialConsole& console) {
    console.addCommand("scan", "scan                       - find servos (IDs 1..20)", [this](SerialConsole&) {
        scanBus();
    });

    console.addCommand("ping", "ping <id>                  - ping a servo", [this](SerialConsole& c) {
        const int id = c.nextInt(-1);
        bool isPresent = false;

        {
            const HardwareLock lock;

            isPresent = servo_.ping(id) != -1;
        }

        if (isPresent) {
            Serial << "servo " << id << " OK" << endl;
        } else {
            Serial << "servo " << id << " no response" << endl;
        }
    });

    console.addCommand("info", "info <id>                  - version, limits, live feedback", [this](SerialConsole& c) {
        printServoInfo((uint8_t)c.nextInt(-1));
    });

    console.addCommand("feedback", "feedback <id>              - live position/speed/load/etc", [this](SerialConsole& c) {
        printFeedback((uint8_t)c.nextInt(-1));
    });

    console.addCommand("id", "id <from> <to>             - change ID (ONLY ONE servo on bus!)", [this](SerialConsole& c) {
        const int from = c.nextInt(-1);
        const int to = c.nextInt(-1);

        if (from < 1 || from > 253 || to < 1 || to > 253) {
            Serial << "usage: id <from 1..253> <to 1..253>" << endl;

            return;
        }

        bool isOk = false;

        {
            const HardwareLock lock;

            isOk = servo_.setId((uint8_t)from, (uint8_t)to);
        }

        if (isOk) {
            Serial << "ID changed " << from << " -> " << to << endl;
        } else {
            Serial << "ID change failed (is exactly one servo connected?)" << endl;
        }
    });

    console.addCommand("move", "move <id> <pos> [spd] [acc] - position 0..4095 (multi-turn in step mode)", [this](SerialConsole& c) {
        const int id = c.nextInt(-1);
        const int pos = c.nextInt(-100000);
        const int speed = c.nextInt(0);
        const int acc = c.nextInt(0);

        if (id < 1 || pos < -32767 || pos > 32767) {
            Serial << "usage: move <id> <pos -32767..32767> [speed] [acc]" << endl;

            return;
        }

        {
            const HardwareLock lock;

            // Route the beam's own servo through the wrapper so its idea of the
            // commanded angle — and therefore the telemetry — stays truthful.
            if ((uint8_t)id == beam_.id() && speed == 0 && acc == 0) {
                beam_.setAngle(beam_.positionToAngle(pos));
            } else {
                servo_.writePos((uint8_t)id, (int16_t)pos, (uint16_t)speed, (uint8_t)acc);
            }
        }

        Serial << "move " << id << " -> " << pos << endl;
    });

    console.addCommand("sync", "sync <pos> <id> [id...]    - move several servos together", [this](SerialConsole& c) {
        const int pos = c.nextInt(-1);

        if (pos < 0 || pos > 4095) {
            Serial << "usage: sync <pos 0..4095> <id> [id...]" << endl;

            return;
        }

        uint8_t ids[Config::kMaxSyncServos];
        int16_t positions[Config::kMaxSyncServos];
        uint16_t speeds[Config::kMaxSyncServos];
        uint8_t accs[Config::kMaxSyncServos];
        uint8_t count = 0;

        for (int id = c.nextInt(-1); id > 0 && count < Config::kMaxSyncServos; id = c.nextInt(-1)) {
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

        {
            const HardwareLock lock;

            servo_.syncWritePos(ids, count, positions, speeds, accs);
        }

        Serial << "sync " << count << " servo(s) -> " << pos << endl;
    });

    console.addCommand("syncread", "syncread <id> [id...]      - read feedback from several at once", [this](SerialConsole& c) {
        uint8_t ids[Config::kMaxSyncServos];
        uint8_t count = 0;

        for (int id = c.nextInt(-1); id > 0 && count < Config::kMaxSyncServos; id = c.nextInt(-1)) {
            ids[count++] = (uint8_t)id;
        }

        if (count == 0) {
            Serial << "usage: syncread <id> [id...]" << endl;

            return;
        }

        St3215::ServoFeedback feedback[Config::kMaxSyncServos];

        {
            const HardwareLock lock;

            servo_.syncReadFeedback(ids, count, feedback);
        }

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

    console.addCommand("speed", "speed <id> <val> [acc]     - continuous speed (needs mode 1)", [this](SerialConsole& c) {
        const int id = c.nextInt(-1);
        const int value = c.nextInt(0);
        const int acc = c.nextInt(0);

        if (id < 1) {
            Serial << "usage: speed <id> <value> [acc]  (set mode 1 first)" << endl;

            return;
        }

        {
            const HardwareLock lock;

            servo_.writeSpeed((uint8_t)id, (int16_t)value, (uint8_t)acc);
        }

        Serial << "speed " << id << " = " << value << endl;
    });

    console.addCommand("mode", "mode <id> [0|1|2|3]        - position/speed/pwm/step (omit to read)", [this](SerialConsole& c) {
        const int id = c.nextInt(-1);

        if (id < 1) {
            Serial << "usage: mode <id> [0=pos 1=speed 2=pwm 3=step]" << endl;

            return;
        }

        const int mode = c.nextInt(-1);

        if (mode < 0) {
            const int current = readByteLocked((uint8_t)id, St3215::Register::OperatingMode);

            if (current < 0) {
                Serial << "servo " << id << " no response" << endl;

                return;
            }

            Serial << "mode " << id << " = " << current << " (" << kModeNames[current & 0x03] << ")" << endl;

            return;
        }

        if (mode > 3) {
            Serial << "usage: mode <id> [0=pos 1=speed 2=pwm 3=step]" << endl;

            return;
        }

        bool isOk = false;

        {
            const HardwareLock lock;

            isOk = servo_.setMode((uint8_t)id, (St3215::Mode)mode);
        }

        if (isOk) {
            Serial << "mode " << id << " = " << mode << endl;
        } else {
            Serial << "mode change failed" << endl;
        }
    });

    console.addCommand("calibrate", "calibrate <id>             - set current position as mid (2047)", [this](SerialConsole& c) {
        const int id = c.nextInt(-1);
        bool isOk = false;

        {
            const HardwareLock lock;

            isOk = servo_.calibrateMid((uint8_t)id);
        }

        if (isOk) {
            Serial << "calibrated servo " << id << " mid-point" << endl;
        } else {
            Serial << "calibrate failed" << endl;
        }
    });

    console.addCommand("anglelimit", "anglelimit <id> [min] [max] - position limits (omit to read, 0/0 = none)", [this](SerialConsole& c) {
        const int id = c.nextInt(-1);

        if (id < 1) {
            Serial << "usage: anglelimit <id> [min 0..4095] [max 0..4095]" << endl;

            return;
        }

        const int low = c.nextInt(-1);
        const int high = c.nextInt(-1);

        if (low < 0) {
            const int currentLow = readWordLocked((uint8_t)id, St3215::Register::MinAngleLimitL);
            const int currentHigh = readWordLocked((uint8_t)id, St3215::Register::MaxAngleLimitL);

            if (currentLow < 0) {
                Serial << "servo " << id << " no response" << endl;

                return;
            }

            Serial << "angle limits " << id << " = " << currentLow << ".." << currentHigh << (currentLow == 0 && currentHigh == 0 ? " (none)" : "") << endl;

            return;
        }

        if (low > 4095 || high < 0 || high > 4095) {
            Serial << "usage: anglelimit <id> [min 0..4095] [max 0..4095]" << endl;

            return;
        }

        bool isOk = false;

        {
            const HardwareLock lock;

            isOk = servo_.setAngleLimits((uint8_t)id, (uint16_t)low, (uint16_t)high);
        }

        if (isOk) {
            Serial << "angle limits " << id << " = " << low << ".." << high << endl;
        } else {
            Serial << "set angle limits failed" << endl;
        }
    });

    console.addCommand("torquelimit", "torquelimit <id> [0..1000] - output torque limit (omit to read)", [this](SerialConsole& c) {
        const int id = c.nextInt(-1);

        if (id < 1) {
            Serial << "usage: torquelimit <id> [0..1000]" << endl;

            return;
        }

        const int limit = c.nextInt(-1);

        if (limit < 0) {
            const int current = readWordLocked((uint8_t)id, St3215::Register::TorqueLimitL);

            if (current < 0) {
                Serial << "servo " << id << " no response" << endl;

                return;
            }

            Serial << "torque limit " << id << " = " << current << endl;

            return;
        }

        if (limit > 1000) {
            Serial << "usage: torquelimit <id> [0..1000]" << endl;

            return;
        }

        bool isOk = false;

        {
            const HardwareLock lock;

            isOk = servo_.setTorqueLimit((uint8_t)id, (uint16_t)limit);
        }

        if (isOk) {
            Serial << "torque limit " << id << " = " << limit << endl;
        } else {
            Serial << "set torque limit failed" << endl;
        }
    });

    console.addCommand("pid", "pid <id> [kp] [kd] [ki]    - position loop gains (omit to read)", [this](SerialConsole& c) {
        const int id = c.nextInt(-1);

        if (id < 1) {
            Serial << "usage: pid <id> [kp] [kd] [ki]  (each 0..255)" << endl;

            return;
        }

        const int kp = c.nextInt(-1);
        const int kd = c.nextInt(-1);
        const int ki = c.nextInt(-1);

        if (kp < 0) {
            const int currentKp = readByteLocked((uint8_t)id, St3215::Register::PositionKp);
            const int currentKd = readByteLocked((uint8_t)id, St3215::Register::PositionKd);
            const int currentKi = readByteLocked((uint8_t)id, St3215::Register::PositionKi);

            if (currentKp < 0) {
                Serial << "servo " << id << " no response" << endl;

                return;
            }

            Serial << "pid " << id << " = " << currentKp << "/" << currentKd << "/" << currentKi << endl;

            return;
        }

        if (kp > 255 || kd < 0 || kd > 255 || ki < 0 || ki > 255) {
            Serial << "usage: pid <id> [kp] [kd] [ki]  (each 0..255)" << endl;

            return;
        }

        bool isOk = false;

        {
            const HardwareLock lock;

            isOk = servo_.setPid((uint8_t)id, (uint8_t)kp, (uint8_t)kd, (uint8_t)ki);
        }

        if (isOk) {
            Serial << "pid " << id << " = " << kp << "/" << kd << "/" << ki << endl;
            Serial << "note: EEPROM only - edit Config.hpp to make it survive a reflash" << endl;
        } else {
            Serial << "set pid failed" << endl;
        }
    });

    console.addCommand("deadband", "deadband <id> [cw] [ccw]   - position deadband in steps (omit to read)", [this](SerialConsole& c) {
        const int id = c.nextInt(-1);

        if (id < 1) {
            Serial << "usage: deadband <id> [cw 0..255] [ccw 0..255]" << endl;

            return;
        }

        const int cw = c.nextInt(-1);
        const int ccw = c.nextInt(-1);

        if (cw < 0) {
            const int currentCw = readByteLocked((uint8_t)id, St3215::Register::CwDeadband);
            const int currentCcw = readByteLocked((uint8_t)id, St3215::Register::CcwDeadband);

            if (currentCw < 0) {
                Serial << "servo " << id << " no response" << endl;

                return;
            }

            Serial << "deadband " << id << " = " << currentCw << "/" << currentCcw << " steps" << endl;

            return;
        }

        if (cw > 255 || ccw < 0 || ccw > 255) {
            Serial << "usage: deadband <id> [cw 0..255] [ccw 0..255]" << endl;

            return;
        }

        bool isOk = false;

        {
            const HardwareLock lock;

            isOk = servo_.setDeadband((uint8_t)id, (uint8_t)cw, (uint8_t)ccw);
        }

        if (isOk) {
            Serial << "deadband " << id << " = " << cw << "/" << ccw << " steps" << endl;
        } else {
            Serial << "set deadband failed" << endl;
        }
    });

    console.addCommand("unload", "unload <id> [mask]         - faults that release torque (omit to read)", [this](SerialConsole& c) {
        const int id = c.nextInt(-1);

        if (id < 1) {
            Serial << "usage: unload <id> [mask]  (0x01 volt 0x02 sensor 0x04 temp 0x08 current 0x20 overload)" << endl;

            return;
        }

        const int mask = c.nextInt(-1);

        if (mask < 0) {
            const int current = readByteLocked((uint8_t)id, St3215::Register::UnloadingCondition);

            if (current < 0) {
                Serial << "servo " << id << " no response" << endl;

                return;
            }

            Serial << "unload condition " << id << " = " << current << " (";
            printFaultNames((uint8_t)current);
            Serial << ")" << endl;

            return;
        }

        if (mask > 255) {
            Serial << "usage: unload <id> [mask]  (0x01 volt 0x02 sensor 0x04 temp 0x08 current 0x20 overload)" << endl;

            return;
        }

        bool isOk = false;

        {
            const HardwareLock lock;

            isOk = servo_.setUnloadingCondition((uint8_t)id, (uint8_t)mask);
        }

        if (isOk) {
            Serial << "unload condition " << id << " = " << mask << endl;
        } else {
            Serial << "set unload condition failed" << endl;
        }
    });

    console.addCommand("stop", "stop [id]                  - stop & hold all known servos (or one)", [this](SerialConsole& c) {
        const int id = c.nextInt(-1);

        if (id > 0) {
            bool isOk = false;

            {
                const HardwareLock lock;

                isOk = servo_.stop((uint8_t)id);
            }

            if (isOk) {
                Serial << "stopped servo " << id << endl;
            } else {
                Serial << "stop failed for servo " << id << endl;
            }

            return;
        }

        {
            const HardwareLock lock;

            for (uint8_t i = 0; i < discoveredCount_; i++) {
                servo_.stop(discoveredIds_[i]);
            }
        }

        Serial << "stopped " << discoveredCount_ << " servo(s)" << endl;
    });

    console.addCommand("relax", "relax [id]                 - release torque / free-wheel (all via broadcast, or one)", [this](SerialConsole& c) {
        const int id = c.nextInt(-1);

        if (id > 0) {
            {
                const HardwareLock lock;

                servo_.setTorque((uint8_t)id, false);
            }

            Serial << "relaxed servo " << id << endl;

            return;
        }

        {
            const HardwareLock lock;

            servo_.relaxAll();
        }

        Serial << "relaxed all servos (broadcast)" << endl;
    });

    console.addCommand("torque", "torque <id> [0|1]          - torque off / on (omit to read)", [this](SerialConsole& c) {
        const int id = c.nextInt(-1);

        if (id < 1) {
            Serial << "usage: torque <id> [0|1]" << endl;

            return;
        }

        const int on = c.nextInt(-1);

        if (on < 0) {
            const int current = readByteLocked((uint8_t)id, St3215::Register::TorqueEnable);

            if (current < 0) {
                Serial << "servo " << id << " no response" << endl;

                return;
            }

            Serial << "torque " << id << " = " << current << (current != 0 ? " (holding)" : " (free)") << endl;

            return;
        }

        {
            const HardwareLock lock;

            servo_.setTorque((uint8_t)id, on != 0);
        }

        Serial << "torque " << id << " = " << (on != 0) << endl;
    });
}
