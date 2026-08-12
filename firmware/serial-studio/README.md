# Serial Studio dashboard

`brainbot.ssproj` is a [Serial Studio](https://serial-studio.com) project for the
telemetry the firmware emits while `debug on` is active.

## Usage

1. Flash the firmware, close any other serial monitor (only one app can hold the port).
2. Serial Studio → *Project Editor* → open `brainbot.ssproj`, or drop the file on the
   *Setup* panel's project selector.
3. In *Setup*, pick the board's COM port (115200 8N1, already set by the project) and
   press *Connect*.
4. Type `debug on` in Serial Studio's console — the dashboard starts updating. `tilt on`
   drives the beam back and forth so there is something to watch.

The beam boots relaxed and takes torque only when something asks it to move, so
`angle 0` or any test command is what wakes it up.

Serial Studio's console shows every incoming byte and has no way to filter frames
out of it — its toolbar offers only a Text/Hex toggle. A command's reply therefore
scrolls away within a frame or two while `debug` is on, so use `debug off` when you
need to read output. `noisetest` and `rolltest` do this for you and leave it off,
since their reports run to dozens of lines.

## Balancing

| Command | What it does |
| ------- | ------------ |
| `ctrl [name\|off]` | Installs a control strategy, or lists what is available. `off` levels the beam. |
| `target <mm>` | Where the ball should be held. |
| `params` | Lists the active controller's gains. |
| `set <name> <value>` | Changes one, live. Tuning a ball & beam takes dozens of small adjustments; a reflash between each is the difference between an evening and a week. |

## Characterization commands

| Command | What it is for |
| ------- | -------------- |
| `debugrate <hz>` | Frame rate. Raise to 100 Hz or more before a step test, or the transient is aliased — the servo settles a small step within a sample or two at the default 33 Hz. |
| `steptest <deg> [holdMs]` | Square wave. Run it small (0.2–1 deg) as well as large: a regulator lives in the small-signal regime, and the deadband and backlash only show up there. |
| `sinetest <deg> <hz>` | Sine sweep. Gain and phase across the intended crossover (~0.3 Hz) are what actually set the closed loop's stability margin — in closed loop the servo never sees a step, only a setpoint moving every tick. |
| `angle <deg>` | Command the beam directly; stops any running test. |
| `trim <deg>` | Servo angle at which the beam is actually level. |
| `deadband <id> <cw> <ccw>` | Position deadband in encoder steps. The factory default of 1 costs ~0.09 deg of standing error; 0 holds the exact count but can hunt at high Kp. |
| `noisetest [seconds]` | Sensor noise with the ball held still. Reports the spread of the readings *and* the spread of the differences between consecutive readings — the second is what a derivative term amplifies, and it is the number that decides how much filtering the estimator needs. |
| `budget <us>` | VL53L0X integration time. Longer is quieter but slower, and the optimum is a real trade rather than "more is better": latency costs the D term as much as noise does. |
| `sensor <default\|long\|fast\|accurate>` | Preset signal-rate and VCSEL-period profiles. |
| `rolltest <deg> [ms]` | Holds a tilt, fits a parabola to the ball's travel, and reports acceleration per degree — the plant constant, including its sign. Put the ball at the uphill end first so it has room to roll. |
| `est [accel] [q] [gate]` | Estimator tuning and live state. Omit the values to read. |
| `sensorreset` | Re-initializes the distance sensor. |
| `state` | Ball position and velocity, beam angle, sensor and controller status. |

## Frame format

The firmware prints one frame every 30 ms (~33 Hz) by default, adjustable with
`debugrate`:

```
$<distance>,<beamAngle>,<targetAngle>,<position>,<speed>,<load>,<current>,<voltage>,<temperature>,<estPosition>,<estVelocity>,<target>;
```

Serial Studio is configured for *start + end delimiter* framing with `$` and `;`, plain
text (UTF-8) decoding, and the default comma-splitting parser. Anything printed outside
those delimiters — command responses, `help`, scan results — is not a frame, so the
interactive console keeps working while frames stream.

| Index | Dataset       | Units   | Source                                       |
| ----- | ------------- | ------- | -------------------------------------------- |
| 1     | Ball Distance | mm      | VL53L0X, continuous ranging (0 if absent)     |
| 2     | Beam Angle    | deg     | Measured servo position, 0 deg = centre       |
| 3     | Target Angle  | deg     | Last commanded position for the beam servo    |
| 4     | Position      | steps   | Raw encoder position, 0..4095                 |
| 5     | Speed         | steps/s | Signed                                        |
| 6     | Load          | 0.1%    | Signed, -1000..1000                           |
| 7     | Current       | mA      | Signed                                        |
| 8     | Voltage       | V       | Bus voltage                                   |
| 9     | Temperature   | C       | Servo case temperature                        |
| 10    | Est. position | mm      | Kalman-filtered ball position                 |
| 11    | Est. velocity | mm/s    | Kalman-filtered ball velocity, + away from sensor |
| 12    | Target        | mm      | Where the controller is holding the ball          |

Fields 10-12 were added after the first nine and are plotted by the *Ball Estimate*
and *Ball Velocity* groups. Serial Studio maps datasets by index, so appending fields
never disturbs the existing ones — but note that `widgetRefs` addresses widgets by a
*global* per-type counter, so inserting a dataset anywhere but the end silently
renumbers every widget reference after it.

Fields 2-9 come from a single bulk read of the beam servo (`Config::kBeamServoId`),
refreshed once per control tick (100 Hz) on core 1. If that read fails, the previous
values are repeated rather than emitting zeros. Raising `debugrate` above 100 Hz
therefore repeats servo fields between ticks; the distance field repeats above ~33 Hz
for the same reason, since that is as fast as the VL53L0X produces readings.

The same holds for the distance: the VL53L0X signals a failed measurement with a large
value (8190 out of range, 65535 on error) instead of an error, so readings above
`kMaxDistanceMm` (600 mm, the end of the beam) are discarded and the last known position
is held. Adjust that constant if the beam length changes.

## Version note

The file uses the Serial Studio 4.x project schema (`schemaVersion: 3`, written as
4.0.3), which stores the connection under `sources[]`. Serial Studio 3.x expects those
keys (`frameStart`, `frameEnd`, `frameDetection`, `decoder`, `frameParserCode`) at the
top level instead — if you are on 3.x, either upgrade or move that block up a level.
