#pragma once

#include <stddef.h>
#include <stdint.h>

/**
 * Wiring and tuning constants for the ball & beam rig.
 *
 * Anything that describes the physical machine lives here, so a change of pin,
 * servo, or beam length is a one-line edit rather than a hunt through the
 * modules that use it.
 */
namespace Config {

// --- Servo bus ---

// Single GPIO shared with the servo bus, through a ~1k series resistor.
constexpr unsigned int kServoPin = 23;

// STS3215 servos ship at 1 Mbps.
constexpr unsigned int kServoBaud = 1000000;

// Servo driving the beam, coupled directly to the pivot at the beam's centre.
constexpr uint8_t kBeamServoId = 3;

// Highest ID probed by the "scan" command.
constexpr uint8_t kMaxScanId = 20;

// Most servos a single "sync"/"syncread" command can address.
constexpr size_t kMaxSyncServos = 8;

// --- Beam servo tuning ---

// Applied at boot so the rig behaves identically after a servo swap or an
// EEPROM reset, rather than depending on gains someone typed in once.
//
// The factory default (Kp 10) is far too soft for the beam's inertia: a 4 deg
// step slews at ~19 deg/s and then creeps onto target for several seconds.
// Kp 120 / Kd 120 slews at ~40 deg/s and settles a 0.5 deg step within a sample
// or two with no measurable overshoot.
//
// Ki is deliberately zero. The servo's deadband hides the final encoder count
// from its own integrator, so integral action buys nothing at rest and only
// winds up during the saturated part of a transient. Integral action belongs in
// the outer ball loop, where it can actually see the error it is correcting.
constexpr uint8_t kBeamServoKp = 120;
constexpr uint8_t kBeamServoKd = 120;
constexpr uint8_t kBeamServoKi = 0;

// Position error the servo treats as "arrived", in encoder steps.
//
// Zero, and it matters far more than it looks. The factory default of 1 step
// creates a dead zone the beam cannot resolve inside, which is invisible on a
// 4 deg swing and crippling on a small one: measured small-signal gain at
// +-0.5 deg was 0.66 with a deadband of 1, and 0.94 with it at 0. A regulator
// spends all its time in exactly that small-signal regime, so the difference is
// a third of the loop gain right at the operating point.
constexpr uint8_t kBeamServoDeadband = 0;

// Beam angles beyond this are clamped. The controller never needs more than a
// few degrees, so this is a backstop against a runaway command dumping the ball
// off the end or driving the beam into the bench.
constexpr float kMaxBeamAngleDegrees = 12.0f;

// --- Distance sensor ---

// VL53L0X sits on Wire1 at the left endstop, looking down the beam.
constexpr unsigned int kDistanceSensorSdaPin = 14;
constexpr unsigned int kDistanceSensorSclPin = 15;

// Continuous ranging period; the sensor produces a new reading at roughly this
// rate and the control tick consumes them as they land.
constexpr uint32_t kRangingPeriodMs = 30;

// Readings outside this window are not measurements. The VL53L0X reports failed
// ranging as a large value (8190 out of range, 65535 on error) rather than as an
// error, and cannot resolve closer than ~30 mm.
constexpr uint16_t kMinDistanceMm = 30;
constexpr uint16_t kMaxDistanceMm = 600;

// --- Loop rates ---

// Control tick on core 1. Faster than the sensor on purpose: the estimator
// predicts between measurements, so the actuator is driven smoothly rather than
// in 30 ms staircases.
constexpr unsigned long kControlIntervalMs = 10;

// Telemetry frame period on core 0. ~33 Hz by default, which is enough for
// watching the ball but aliases the servo's step response; "debugrate" raises it
// for characterization runs.
constexpr unsigned long kDefaultDebugIntervalMs = 30;
constexpr unsigned long kMinDebugIntervalMs = 2;
constexpr unsigned long kMaxDebugIntervalMs = 1000;

// --- Ball position estimate ---

// Measurement noise grows with distance, because a time-of-flight sensor's
// return signal falls as 1/d^2 and its noise with the square root of that. Fitted
// to two measured points: sigma 13.5 mm at 292 mm, 31.9 mm at 546 mm.
//
//   sigma(d) = kSensorNoiseBaseMm + kSensorNoiseQuadratic * d^2
//
// Feeding the filter one fixed noise figure would make it trust the far end of
// the beam six times more than it deserves, which is where it can least afford
// to be wrong.
constexpr float kSensorNoiseBaseMm = 6.2f;
constexpr float kSensorNoiseQuadratic = 8.6e-5f;

// Ball acceleration per degree of beam tilt, mm/s^2 per degree, signed so that
// positive means the ball accelerates towards larger distance readings.
//
// Theory gives about 110 for a ball rolling without slipping. "rolltest"
// measures it on the actual rig, including the sign, which depends on how the
// servo happens to be mounted and is not worth guessing.
constexpr float kAccelPerDegree = 110.0f;

// Acceleration the model does not account for: rolling friction, the ball
// slipping rather than rolling, the beam flexing. Sets how quickly the filter is
// willing to abandon its prediction in favour of the measurements.
constexpr float kProcessNoiseMmPerS2 = 400.0f;

// A measurement further than this many standard deviations from the prediction
// is treated as an outlier rather than as news. Generous, because the cost of
// rejecting a real measurement is worse than the cost of accepting a bad one.
constexpr float kInnovationGateSigma = 4.0f;

// Consecutive rejections before the filter concludes its state is wrong rather
// than the measurements, and reinitializes. This is what happens when the ball
// is picked up and put down somewhere else.
constexpr uint8_t kMaxConsecutiveRejects = 6;

// Initial velocity uncertainty when the filter starts or restarts.
constexpr float kInitialVelocitySigmaMmPerS = 300.0f;

// --- Beam geometry ---

// Distance reading with the ball at the centre of travel, used as the default
// setpoint. Measured, not derived: the sensor sits behind the endstop face.
constexpr float kBeamCentreMm = 300.0f;

} // namespace Config
