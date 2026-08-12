#include <Arduino.h>

#include "BallBeamSystem.hpp"
#include "Config.hpp"
#include "SerialConsole.hpp"
#include "St3215.hpp"
#include "SwUartPioBus.hpp"
#include "console/RigCommands.hpp"
#include "console/ServoCommands.hpp"
#include "control/controllers/CascadeBallBeamController.hpp"
#include "control/controllers/PidBallBeamController.hpp"
#include "hardware/BeamServo.hpp"
#include "hardware/HardwareLock.hpp"
#include "hardware/Vl53l0xBallSensor.hpp"
#include "stream_operators.hpp"
#include "telemetry/TelemetryStream.hpp"

// Core 0 owns the console and telemetry; core 1 owns the control tick. The split
// exists so that a host which stops draining USB — or a "scan" waiting out twenty
// reply timeouts — cannot add jitter to, or stall, the loop driving the beam.
static SwUartPioBus bus(Config::kServoPin, Config::kServoBaud);
static St3215 servo(bus);
static BeamServo beam(servo, Config::kBeamServoId);
static Vl53l0xBallSensor distanceSensor;
static BallBeamSystem rig(beam, distanceSensor);

static SerialConsole console(Serial);
static TelemetryStream telemetry(Serial, rig);
static ServoCommands servoCommands(servo, beam);
static RigCommands rigCommands(rig, telemetry, distanceSensor);

// Control strategies, offered to the console by name. Adding another is a class
// implementing BallBeamController plus one addController() call below.
static PidBallBeamController pidController;
static CascadeBallBeamController cascadeController;

void setup() {
    Serial.begin(115200);
    Serial.ignoreFlowControl(true);

    // Only wait for a monitor when asked to. Writes cannot block regardless —
    // ignoreFlowControl() above sees to that — so running headless is safe.
    if (Config::kWaitForSerialOnBoot) {
        while (!Serial && millis() < 8000) {
            delay(50);
        }
    }

    delay(100);

    Serial << "Brainbot ball & beam" << endl;

    HardwareLock::init();

    if (!servo.begin()) {
        Serial << "Failed to start PIO UART (no free state machine)" << endl;

        return;
    }

    rigCommands.addController(&pidController);
    rigCommands.addController(&cascadeController);

    servoCommands.registerCommands(console);
    rigCommands.registerCommands(console);
    servoCommands.scanBus();

    // Every servo starts in position mode and free-wheeling. Nothing takes
    // torque until a command asks it to, so power-up cannot fling the beam.
    servo.setMode(St3215::kBroadcastId, St3215::Mode::Position);
    servo.relaxAll();

    // All hardware is brought up here, on core 0, before core 1 starts ticking.
    // That keeps the boot output in one piece and guarantees the control tick
    // never sees half-configured hardware.
    Serial << "Configuring beam servo " << Config::kBeamServoId << ": kp=" << Config::kBeamServoKp << " kd=" << Config::kBeamServoKd << " ki=" << Config::kBeamServoKi
           << " deadband=" << Config::kBeamServoDeadband << endl;

    if (!rig.begin()) {
        Serial << "Beam servo did not acknowledge configuration" << endl;
    }

    if (distanceSensor.isPresent()) {
        Serial << "VL53L0X distance sensor initialized" << endl;
    } else {
        Serial << "Failed to initialize VL53L0X distance sensor" << endl;
    }

    console.printHelp();

    Serial << endl;

    if (Config::kAutoStartController) {
        // Installing a controller takes torque and starts the loop, so the rig
        // balances on power alone with no console attached.
        rig.setController(&cascadeController);

        Serial << "Balancing with '" << cascadeController.name() << "' at " << rig.target() << " mm. 'ctrl off' to stop." << endl;
    } else {
        Serial << "Beam is relaxed. 'angle 0' or a test command takes torque." << endl;
    }
}

void loop() {
    console.loop();
    telemetry.loop();
}

void setup1() {
    // Nothing to bring up here: core 0 owns initialization, and isReady() gates
    // the tick until it has finished.
}

void loop1() {
    static unsigned long lastTickMs = 0;

    if (!rig.isReady()) {
        return;
    }

    const unsigned long nowMs = millis();

    if (nowMs - lastTickMs < Config::kControlIntervalMs) {
        return;
    }

    lastTickMs = nowMs;

    rig.tick();
}
