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

## Frame format

The firmware prints one frame every 30 ms (~33 Hz):

```
$<distance>,<beamAngle>,<targetAngle>,<position>,<speed>,<load>,<current>,<voltage>,<temperature>;
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

Fields 2-9 come from a single bulk read of the beam servo (`kTiltServoId`). If that read
fails, the previous values are repeated rather than emitting zeros.

The same holds for the distance: the VL53L0X signals a failed measurement with a large
value (8190 out of range, 65535 on error) instead of an error, so readings above
`kMaxDistanceMm` (600 mm, the end of the beam) are discarded and the last known position
is held. Adjust that constant if the beam length changes.

## Version note

The file uses the Serial Studio 4.x project schema (`schemaVersion: 3`, written as
4.0.3), which stores the connection under `sources[]`. Serial Studio 3.x expects those
keys (`frameStart`, `frameEnd`, `frameDetection`, `decoder`, `frameParserCode`) at the
top level instead — if you are on 3.x, either upgrade or move that block up a level.
