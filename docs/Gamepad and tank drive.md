# Gamepad and tank drive

Sparky drives with an Xbox One S controller (model 1708, BLE firmware 5.23) over Bluetooth Low Energy. Verified on the robot 2026-11-20, on battery, on the floor.

## Using it

Tank mode starts at power-on with both drives disarmed.

1. Press the Xbox button. A bonded controller reconnects by itself; a new one needs `gamepad pair` over serial while it is in pairing mode (hold the pair button on top until the logo flashes fast).
2. Hold **LB + RB** for a second with both sticks centred to arm.
3. **Left stick** up/down drives, **right stick** left/right turns (alone it spins in place). **Y** toggles fast mode (double speed).
4. **B** disarms. A tap on the screen, a lost controller, a drive fault or the pack below 16.5 V also quick-stop both tracks and disarm. The battery switch is the hard stop.

Defaults: 300 rpm at full stick (600 in fast mode), 1000 rpm/s acceleration, turn 100 %, 12 % stick deadzone. Serial commands change them until the next restart: `tank max <rpm>`, `tank turn <percent>`, `tank accel <rpm/s>`, `tank nodes <left> <right>`, `tank invert left|right`, `tank status`, `tank stop`, `tank off` (hands the bus back to the console screens), `tank on`.

## How it works

- **Code:** `firmware/main/debug/GamepadTest.cpp` (Bluetooth and the gamepad view), `debug/TankTest.cpp` (tank mode). Both are still test code; see [Milestones](Milestones.md).
- **Drives:** left node 2, right node 1 inverted (positive velocity runs the right track backwards; the left motor is mounted the other way round). Profile velocity mode (`6060` = 3), target `60FF` in counts/s = rpm × `608F:01` / 60, ramps in `6083`/`6084`. Targets go out only on change, plus a refresh twice a second.
- **Arming** clears a fault or a latched error code (`603F`), lowers the undervoltage limit `2001:10` to 12 V at run time (D-005), sets the mode and ramps, and walks the CiA 402 state machine. It checks state and bus voltage twice a second while armed.
- **Disarming** sends quick stop to both, waits up to 1.5 s for both to drop below 5 rpm, then disables them.
- **The bus:** while tank mode is on, the motor service lends out the bus (`MotorService::setBusSuspended`) and keeps only bus-off recovery.
- **Mixing:** left = throttle + turn, right = throttle − turn; both are scaled down together when one side would pass full speed.

## Bluetooth: what it took

- **Link to the C6:** ESP-Hosted 3.1.0 on the P4 talks to the C6 over SDIO on the default pins (CLK GPIO18, CMD GPIO19, D0–D3 GPIO14–17, C6 reset GPIO54). NimBLE runs on the P4.
- **C6 firmware:** the factory firmware reports version 0.0.0 and doesn't answer ESP-Hosted 3.x RPCs. `bring_up_controller = false` works because its BLE controller is already up from boot. Wi-Fi will most likely need the C6 reflashed.
- **Internal RAM** is tight (about 21 KB free with the display up): `CONFIG_BT_NIMBLE_MEM_ALLOC_MODE_EXTERNAL` and `CONFIG_ESP_HOSTED_DFLT_TASK_FROM_SPIRAM` move NimBLE and ESP-Hosted's task stacks to PSRAM.
- **HID host:** ESP-IDF's `esp_hidh` reads no report map or reports from this controller and hangs if the link drops during discovery. A small GATT client works instead: encrypt, find the HID service `0x1812`, read the report map `0x2A4B` (283 bytes; without this read the controller stays in pairing mode and sends nothing), subscribe to the notifying report (handle 30).
- **Pairing:** LE Secure Connections fails ("DHKey check failed"), so legacy Just Works (`sm_sc = 0`). Don't distribute our IRK (`sm_our_key_dist` = ENC only): with it the controller never reconnects after sleeping.
- **Reconnect:** scan for the bonded address, then connect.
- **Supervision timeout** 0.5 s, so a dead controller shows as a disconnect quickly. The controller asks for 3 s; the connection update handlers clamp it. On an L2CAP update request NimBLE fills `peer_params` and leaves `self_params` null (this crashed the board once).
- **Report layout (16 bytes):** bytes 0–7 sticks (u16, centre `0x8000`), 8–11 triggers (10-bit), 12 hat, 13–14 buttons (A0 B1 X3 Y4 LB6 RB7 View10 Menu11 Xbox12 LS13 RS14), 15 Share (bit 0, Series X/S controllers only).

## Gaps

- **A hung P4 leaves the tracks running** at their last speed (see [Open questions](Open%20questions.md)).
- **A stalled report stream isn't detected**, only a dropped link.
- **The top bar's STOP doesn't reach tank mode**; a tap on the gamepad view does.
