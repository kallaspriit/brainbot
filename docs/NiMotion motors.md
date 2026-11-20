# NiMotion motors

What this project needs to know about the drives: the NiMotion manuals (English and Chinese CANopen manuals, BLM42/57 datasheets), a working production driver for these motors, and bench measurements made while building the NiMotion debug console this firmware started from (October 2026, on a 18 V pack). Measurements are marked with their date.

**The English CANopen manual is wrong about activation, save/restore and `200C:01`.** The Chinese manual is more reliable. Check both before writing anything to a drive.

## Models

Integrated BLDC servos: drive, FOC and a 2500-line magnetic encoder in the motor. No brake (the B suffix means the encoder).

| Model    | Supply   | Rated current | Torque                    | Power |
| -------- | -------- | ------------- | ------------------------- | ----- |
| BLM4203B | 12–36 V  | 1.7 A         | 0.08 Nm                   | 26 W  |
| BLM4205B | 12–36 V  | 2.77 A        | 0.15 Nm                   | 47 W  |
| BLM5709B | 16–52 V  | 5.0 A         | 0.30 Nm (0.60 Nm ≤ 10 s)  | 90 W  |

All: 24 V and 3000 rpm rated, single supply (no separate logic supply), 3 DI, 2 DO, 1 AI. Reverse supply polarity damages the motor. On 18 V the top speed is roughly a quarter lower than rated (estimate, not measured).

Status LEDs: green RUN steady = comms OK, 0.5 s blink = comms alarm. Yellow COM 1 s blink = normal, 0.5 s = warning, 0.25 s = fault.

## Bus

- CANopen, CiA 301 + CiA 402, CAN 2.0A, non-isolated 3-wire port.
- Default: **1 Mbit/s, node ID 1.** Sparky's tracks are nodes 2 (left) and 1 (right). Node ID is `200C:02`, bitrate `200C:03` (8 = 1 Mbit/s, 6 = 500 kbit/s, bench 2026-10-12). **Both take effect only after a power cycle**, not after an NMT reset, **and only if saved** (`1010:01`): an unsaved write reads back but is gone after the power cycle (bench 2026-10-12).
- The drives also speak Modbus RTU over RS485; out of scope.

## How a working driver talks to them

A production pan-tilt unit (PTU) firmware drives these motors; this firmware's CANopen code follows the same sequence.

- **SDO only**: expedited SDO on `0x600+id` / `0x580+id`, 100 ms timeout; segmented upload for strings (`1008`–`100A`). No PDOs in use.
- **Startup**: NMT reset application (`0x81`), poll `200C:02` for up to 2 s, controlword 6 then `0x80` (fault reset), `2002:01 = 0` (CiA 402 control mode), read identity `1018:01-04` (vendor `0x509`, product 3), read power-on time, bus voltage, temperature, polarity.
- **Enable walk**: write `6060` mode → controlword 0 → 6 → 7 → `0x0F` (`0x1F` for homing), waiting for each CiA 402 state. Can't enter velocity mode while the shaft turns. The drive drops to Switch On Disabled about 1.4 s after power-on.
- **Velocity mode**: `6060 = 3`, setpoint `60FF` in encoder counts/s. Position from `6063` (10000 counts/rev).
- **Homing**: `6060 = 6`, method 21, opto sensor on DI1 (`2003:03 = 31`).
- **Position mode** is not used by the PTU. It works on these drives as standard CiA 402; see [Profile position](#profile-position-bench-2026-10-09) below.
- **PDOs:** the PTU's frame decoder expects TPDO1 `0x181` = `6041` + `6061`, TPDO2 `0x281` = `6063` + `6064`, TPDO3 `0x381` = `606C`. Those look like the drives' default mappings, but the PTU never sends NMT Start, so they aren't used. Not checked on our motor (`1A00`–`1A02` not read).

## Objects

The full list is the `NIMOPARAM_*` enum in `nimo.h:81-149`.

| Object                | Meaning                                                                  |
| --------------------- | ------------------------------------------------------------------------ |
| `1003`                | Alarm history                                                            |
| `1010:01` / `1011:01` | Save (`0x65766173`, "save") / restore defaults (`0x64616F6C`, "load")    |
| `1008` / `1009` / `100A` | Device name, hardware version, software version (strings, segmented upload) |
| `1018:01-04`          | Identity: vendor, product, revision, serial                              |
| `2000:xx`             | Motor and I2t settings (rated/max output current in 0.01 A, I2t)         |
| `2001:xx`             | Drive limits (`2001:0F` braking threshold, `2001:10` bus low voltage)    |
| `2001:10`             | Bus undervoltage limit, 1 V, **factory 24 V**, takes effect when stopped. The PTU sets 12. See below |
| `200C:1B`             | Communication watchdog, ms, 0 = off. When on, the drive raises 0x7501 unless the object is rewritten in time |
| `2002:01`             | Control mode (0 = CiA 402)                                               |
| `2003:03`             | DI1 function                                                             |
| `2008:01` / `2008:02` | Speed-loop gain (0.1 Hz) / integral time (0.01 ms; 51200 = no integral) |
| `200B:05`             | DI state                                                                 |
| `200B:0F`             | Power-on time                                                            |
| `200B:15` / `200B:16` | Bus voltage (0.1 V) / temperature. some summaries swap these; 15 is the voltage (bench) |
| `200C:02` / `200C:03` | Node ID / bitrate                                                        |
| `200E:xx`             | Fault behaviour (which alarms are warnings, which self-reset)            |
| `603F`                | Active error code                                                        |
| `6040` / `6041`       | Controlword / statusword                                                 |
| `6060` / `6061`       | Mode of operation / mode display                                         |
| `6063` / `6064`       | Position actual (encoder counts / user units)                            |
| `606C`                | Velocity actual                                                          |
| `6072` / `6073`       | Max torque / max current                                                 |
| `6078`                | Motor current feedback, signed, ‰ of rated current `2000:09` (see below) |
| `6065` / `6067`       | Following error window / position window (counts; 50 and 10 by default) |
| `607A`                | Target position                                                          |
| `607D:01` / `:02`     | Software position limits (min / max), see [Profile position](#profile-position-bench-2026-10-09) |
| `6081`                | Profile (move) velocity, user units/s                                    |
| `608F` / `6091`       | Encoder resolution / gear ratio: user units per turn                     |
| `1017`                | Heartbeat producer time, ms, see [the heartbeat](#stopping-on-a-lost-bus-the-heartbeat-bench-2026-10-11) |
| `60FF`                | Target velocity                                                          |

## Identity strings (bench, 2026-10-09)

Read by segmented SDO upload (NAV-005) from the test motor, serial 141416, 20 times in a row with polling in between, all identical:

| Object | Value |
| ------ | ----- |
| `1008` device name | `NiMotion Servo` |
| `1009` hardware version | `VDDBP6040B-CANopen-C` |
| `100A` software version | `V0.2.5.a5` |

## Current reading `6078`: per mille of rated current, not 0.01 A (bench, 2026-10-09)

**Finding:** `6078` is motor-side current feedback in **per mille of the motor's rated current** (`2000:09`), not 0.01 A as the English and Chinese CANopen manuals' object tables say. Inferred from a power balance, not yet confirmed with a current probe. The console converts it as `6078 × 2000:09 / 1000` and labels it "Motor current".

Evidence, test motor 141416 (BLM42) on a 24 V bench supply:

| Situation | `6078` raw | Supply | As 0.01 A | As ‰ of rated (1.70 A) |
| --- | --- | --- | --- | --- |
| 300 rpm, shaft free | 32 | 0.04 A (0.95 W) | 0.32 A | 0.05 A |
| Low speed, loaded by hand | about 400 | 0.1 A (about 2.4 W) | 4.0 A | 0.68 A |

- This motor reads rated current `2000:09` = 170 (1.70 A) and stator resistance `2000:12` = 1103 (1.103 Ω).
- At 4 A the winding loss alone would be about 18–26 W (13 W even if 1.103 Ω is line to line), ten times the 2.4 W the supply delivered. So 0.01 A is impossible.
- At 0.68 A the winding loss is under 1 W, which fits.
- Per mille of rated current is the CiA 402 standard unit for `6078`, and NiMotion's Modbus manual describes the same value as "0.1 % of rated current". The CANopen manuals' 0.01 A looks like a documentation error.
- **It has no sign** (bench 2026-10-12): +3.0–3.4 % at both +600 and −600 rpm in velocity mode, and positive at a negative torque in torque mode, though the object's type is a signed INT16. So it is a magnitude; whether of the torque-producing current (Iq) or a phase RMS value is unknown. The console's charts draw it as such: the speed shows the direction.


## Speed limits (bench, 2026-10-09)

Test motor 141416 (BLM42): rated speed `2000:0E` = 3000 rpm (matches the BLM42 datasheet's 3000 rpm at 24 V), max motor speed `6080` = 4000 rpm, max profile velocity `607F` = 1 000 000 counts/s (6000 rpm). `2006:09` (max speed threshold, manual default 6000 rpm) is not read. The console limits targets to the rated speed (D-019).

## Profile position (bench, 2026-10-09)

Not used by the PTU firmware; its `nimo_move_absolute`/`relative` are unused and skip the handshake. Works on the test motor (141416) as standard CiA 402, following the Chinese G03 manual:

- **Set-up:** `2002:01 = 0`, `6060 = 1`, ramps `6083`/`6084`, move speed `6081` (user units/s), target `607A` (absolute).
- **New target:** controlword `0x2F` then `0x3F` (bit 5 change immediately, bit 4 new set-point). Wait for statusword bit 12 (set-point acknowledge), then `0x2F`. Acknowledged every time.
- **Halt:** controlword bit 8 (`0x12F`) decelerates on the `6084` ramp (`605D` = 1) and holds. The next GO clears it.
- **Target reached:** statusword bit 10 rises when the position stays within `6067` of the target. It drops after a new set-point, so the console only reports a settle after seeing it drop. While halted, bit 10 means "speed 0" instead (CN:1419).
- **Changing speed or ramps mid-move:** the drive takes `6081`, `6083` and `6084` only with a set-point. Re-sending the same target with "change set immediately" makes the motor re-plan on the fly to the new speed (bench 2026-10-09).
- **Following error:** not seen to trip with the default `6065` window during these tests, including a hand pushing a held shaft (not measured).
- **Wait after enabling.** A set-point sent within a few milliseconds of reaching Operation Enabled runs without the profile: the motor jumps to the target at up to about 2750 rpm, ignoring `6081` (600 rpm) and the ramps, though `6081`/`6083`/`6084` read back correctly. Bench 2026-10-09: four of six moves after a 7 ms state walk peaked at 1616–2747 rpm; moves after a 40 ms walk ramped properly. The console waits 100 ms after enabling before the first set-point; ten moves since followed the profile. Not in the manual sections read so far.
- **Units:** user units equal encoder counts on this motor (`608F` 10000/1, `6091` 1/1).
- **`607D` (software position limits):** reads −2 147 483 628 … 2 147 483 627 here, not the manual's ±65 535 default. A factory-default motor may need the widening of D-020.

**English manual errors for profile position**, against the Chinese G03 manual:
- The handshake is described backwards: release bit 4 after bit 12 goes 1 → 0 (EN:1182). The Chinese manual says after it becomes 1.
- The example swaps `6083` and `6084` (EN:1243).
- `6081`'s range is given as 0–500 000 (EN:5170) against 0–1 000 000 (CN:6610).
- `6067` is described as "exceeds" rather than "within" (EN:5127).

## Faults

- Fault reset: controlword `0x80`.
- Alarm table: `nimo.h:151-221`; full list in `9.1_Alarm_Codes.md`. Common ones: `0x2300` overcurrent, `0x2311` overload, `0x2312` stall, `0x3220` undervoltage, `0x8610` homing timeout, `0xFF02` parameter save.
- **Names come from the Chinese manual's table 9-1** (G03, CN:4934), which lists the codes this drive family raises. The English manual's table has a dozen codes the Chinese one doesn't (`0x2301`, `0x7301`–`0x7305`, `0x8700`, `0xFF0C`…) and some mistranslations (`0xFF02` is a parameter saving fault, not "unexecutable commands"). `firmware/main/protocol/AlarmCodes.cpp` follows the Chinese table and keeps the English-only codes at the end. **Seen on a drive (2026-10-11):** `0x2300` overcurrent (failed motor 159771), `0x2312` stall, `0x3220` undervoltage, `0x7501` communication failure, `0xFF02` save fault.
- `0x2300` overcurrent cannot be reconfigured as a warning. The PTU turns undervoltage and low temperature into self-resetting warnings and stall into a warning via `200E:xx`.
- EMCY on `0x80+id`. Decode little-endian; the PTU firmware decodes it big-endian, which looks like a bug.
- Healthy statusword: `0x0637` or `0x1637`.

## Alarm history, fault reactions and inputs (bench, 2026-10-11)

Read with the `dump` serial command (FLT-001 in the Faults plan) on the test motor 141416, firmware V0.2.5.a5. The dump probes `1000`–`1A03`, `2000`–`203F` and `6000`–`60FF` and found 149 objects in 1.5 s.

- **`1003` alarm history:** 16 entries in `:01`–`:10`, **newest first**. A new alarm goes into `:01` and the rest move down one. Each entry is the **full 32-bit alarm word** in the `200E` format (response code in bits 31–24, self-reset in 23–16, alarm code in 15–0), e.g. `0x01013220` = undervoltage, response 1, resets itself. **`1003:00` reads 0** with all 16 entries full, so it is not the count; the manual's text ("current fault number") fits better than its table. **The history survives a motor power cycle.** No timestamps.
- **`200E` fault reactions:** `:00` = 17, `:01`–`:11` as the manual's table 9-2 lists them (overload, stall, overvoltage, undervoltage, high and low temperature, parameter error, overspeed, communication failure, homing timeout, following error, software limit, limit switch, phase loss, curve planning, target overflow, curve parameters). On this motor all equal the table's defaults except `:09` (communication failure) = `0x03017501` against `0x00017501`.
- **An undervoltage dip** (11 V for a few seconds, motor idle) added `0x01013220` to the history; `603F` was back at 0 once the supply was, since this reaction resets itself.
- **Inputs:** DI1–DI3 functions `2003:03`/`:05`/`:07` = 0 (no function), logic `:04`/`:06`/`:08` = 0; the function numbers are table 7-2 (1 servo enable, 2 alarm reset, 12 pause, 14/15 overtravel, 31 home switch, 38 clear fault history). Virtual inputs are enabled (`200C:07` = 1) but all `2017` functions are 0. `200B:05` (input levels) = 7.
- **Not configured for a PTU:** homing method `6098` = 24 (the PTU requires 21) and DI1 has no home-switch function (the PTU sets 31).
- **`200C:01` (CommunicationSelect) reads 2** on this CAN drive, which supports the gloss "2 ~ CAN" over the documented default of 1. Still never written (safety rules).
- **Profile values after power-on** (not saved by the console): `6081` = 1 000 000, `6083` = 409 600, `6084` = 1 000 000, `607A` = 600. `200B:0F` (power-on time) keeps counting across power cycles.
- `60C4:05`/`:06` are write-only (abort `0x06010001` on read).

**Power-on and power-off leave marks** (bench, 2026-10-11, PSU switched off and on with the console connected):

- **Power-off:** as the supply falls the drive raises undervoltage (EMCY `0x3220`, fault bit set, error register `0x05`) about half a second before it goes silent, and records it in `1003`. So Undervoltage entries in a motor's history are mostly power-offs, not supply problems. The console says "Motor powered off" when the drive is lost while `603F` is `0x3220`.
- **Power-on:** for about a second the drive sits in Ready To Switch On (`0x0221`) with `603F` = `0x7501` and neither the fault nor the warning bit set, sends no EMCY, records nothing, then drops to Switch On Disabled (`0x0260`) with `603F` = 0 on its own. The console treats an error code without the fault or warning bit as inactive. This also explains the 0x7501 seen after power-ups on 2026-10-08 (undervoltage section below).
- **Repeated entries are recorded:** several undervoltage dips in a row on the broken motor (159771) each added an entry, so the drive does record the same alarm twice in a row. The earlier alternating pattern only reflected our test sequence.
- **A console restart adds communication failure** once the console has turned on the heartbeat (D-023): the drive misses it while the console reboots and records `0x7501`.

## Stall (bench, 2026-10-11)

Test motor 141416, factory reactions, velocity mode at 600 rpm, shaft held with pliers on an 18 V supply.

- **It trips by hand.** EMCY `0x2312` about 10 s into the hold; history word `0x03002312` (reaction 3, "pause and lock the shaft", not self-resetting). Chinese manual §9.3: stall is below 5 rpm above the rated current, by an I²t integral against `2000:0A` (max current, 340 = 3.4 A) times `2000:0B` (100). The trip after about 10 s fits a unit of 0.1 s, which a PTU firmware comment also gives; inferred, not measured.
- **No fault and no warning bit.** The drive stays in Operation enabled (`0x1637`) with only `603F` = `0x2312` and error register `0x81`. Anything that judges alarms by the statusword bits misses it; the console counts a non-zero `603F` in Operation enabled as active.
- **While held it keeps pushing at about 1.7 A**, the rated current `2000:09` (observed on the console's current reading). When the shaft is released the drive sends `0x2312` again and an error-reset EMCY (`0x0000`), stops pushing and holds at 0 rpm with about 0.05 A. Why it lets go then is not understood.
- **It stays latched.** `603F` keeps `0x2312` through stop and start; the next enable halts at once. A fault reset (controlword bit 7) clears it, both in Operation enabled and in Switch On Disabled before the state walk.

- **Configurable in `200E:02`:** reaction 0–2 go to Fault and power the stage off (stop per `605E`), 3 is the factory halt and lock, 4 a warning that keeps running, 5 ignore and record (Chinese manual §9.2). **The PTU sets 4**, so a stalled PTU axis keeps pushing. A stalled motor doesn't commutate, so the current sits in one or two phases, which heat faster than at rated current while turning (not quantified).
- **What the PTU firmware does** (read from the code, not tested): it sets `2000:0B` = 3000 (300 s if the unit is 0.1 s), so a stall takes much longer to detect. When `603F` shows overcurrent, overload or stall, `l_check_motor_error` sends a fault reset and starts velocity mode again, up to 3 failed tries before taking the motor out of use; a successful restart sets the count back to 0. So a jammed axis is restarted and keeps pushing; nothing powers it off.
- **The console stops a run on a stall itself when the drive would hold:** quick stop and power stage off when `603F` reads `0x2312` while running and `200E:02` is halt and hold (response code 3, the factory reaction). Other reactions are left to the drive (bench 2026-10-12: a warning keeps it running, and about 2 s into a held stall it adds `0x8311` torque-limiting protection, also a warning). No drive write.

**Following error** (`0x8611`) was not provoked: with the factory window `6065` = 50 units (1.8°) the timeout `6066` is 30000 ms, so the shaft would have to be held for 30 s.

## Undervoltage limit on battery power

**Found on the bench 2026-10-08.** A drive below its bus undervoltage limit (`2001:10`) accepts controlword writes without an SDO abort but stays in Ready To Switch On, so the CiA 402 state walk times out. Our test motor (serial 141416) read 20 V and would not enable at 18.2 V; it ran at 24 V. Its error code read 0x7501 (communication failure) throughout, before and after a power cycle, with the watchdog off; the link to the undervoltage is not understood.

- Factory default is 20 V on firmware V0.2.5.a5 (24 V per the manual's object table; measured after a restore, 2026-10-11), so a factory-fresh motor will not run on an 18 V pack.
- PTU-configured motors have 12 V and should run.
- The console reads `2001:10` on connect and lowers it to 12 V automatically, run-time only, showing the original value (D-005). Lowering it at run time with the drive switched off worked on the bench: the motor then ran at 18 V.

## Quick stop and the communication watchdog (bench, 2026-10-08)

Test motor serial 141416 (BLM42), 18 V, shaft free; NAV-001 in the Navigation shell plan.

- **Stop-related values:** `6085` (quick-stop deceleration) = 500 000 counts/s², `605A` (quick stop option code) = 2, `607F` (max profile velocity) = 1 000 000 counts/s (about 6000 rpm, not limiting).
- **Quick stop works.** `6040 = 0x02` from Operation Enabled: statusword `0x0637` → `0x0217` (Quick Stop Active) while braking → `0x1240` (Switch On Disabled, power stage off), as `605A = 2` predicts. Stop took about 0.35 s from 60 rpm and 0.55 s from 300 rpm. The braking is gentler than 500 000 counts/s² would give (about 20 ms from 60 rpm); the drive seems to limit it some other way.
- **The communication watchdog does not arm at run time.** `200C:1B = 500` is accepted and reads back, but with 2 s of complete bus silence the motor kept running with no alarm, in NMT pre-operational and in NMT Operational alike. Writing 0 back also reads back immediately. Likely it only takes effect after a save and power cycle; that is not tried, because a saved watchdog would go back into a PTU whose firmware never feeds it.
- **NMT Operational disturbs SDO polling:** some velocity reads returned 0 while the motor turned, probably the default TPDOs crowding the bus. Keep the drive in pre-operational.
- **0x7501** (communication failure) was not present after a clean power-up; it was not seen again.

## Stopping on a lost bus: the heartbeat (bench, 2026-10-11)

`200C:1B` doesn't trip on this firmware (above). What does work: with the heartbeat producer `1017` on, a pulled CAN cable leaves the drive's heartbeat unacknowledged, the drive raises EMCY `0x7501` and reacts as the alarm's action word `200E:09` says. The test motor (141416) holds `0x03017501` there: halt on the quick-stop ramp, lock the shaft, self-reset allowed (response code in bits 31-24, alarm table 9-1). Pulling the cable at 60 and at 2000 rpm stopped the motor smoothly and at once; it stayed halted and holding until reconnected. The `0x7501` EMCY and its clearing (`0x0000`, error register `0x11`) arrive after reconnecting.

- `1017` exists and accepts a run-time write (reads back); `1800:01` (TPDO1) is enabled at `0x181`, type 255, no event timer, and silent since the drive stays pre-operational.
- **A console reset stops it too** (bench 2026-10-13, velocity mode): the motor stopped immediately when the board reset; the drive reported `0x7501` and held at standstill, and the console, connecting about 1.2 s later, found it in Operation enabled at 3 rpm and powered it off ("Drive was still running, stopped it").
- Works only while the console is the drive's only partner on the bus. Another node acknowledging the heartbeat would hide the loss.
- *Also seen:* after these tests `6063` (encoder counts) read 180 030 457 while `6064` (user units) stayed near 61 million; both matched earlier the same day. Not investigated.

## Saving parameters and restoring defaults (bench, 2026-10-11)

Test motor 141416, firmware V0.2.5.a5, FLT-006/FLT-007 in the Faults plan. A marker (undervoltage limit `2001:10` set to 21 or 15 V and saved) showed whether a save or a restore really happened, since the motor was already at factory settings.

- **Save** is `1010:01 = 0x65766173` ("save"); **restore** is `1011:01 = 0x64616F6C` ("load"). The drive reports both as supported (`1010:01` and `1011:01` read 1). The PTU's `setup` command writes everything twice before saving.
- **Flash writes need the bus at about 80 % of the rated voltage `2000:07`.** This motor has `2000:07` = 24 V, so the limit is 19.2 V: at 18.2–18.4 V the drive accepts the SDO write, then raises `0xFF02` (parameter saving fault, a self-resetting warning, error register `0x81`) and stores nothing; at 19.2 and 24.3 V both save and restore work. The undervoltage setting plays no part (run-time 12 V and stored 15 V both tried). The 80 % is inferred from two numbers: the measured boundary, and the Chinese manual's note in its save section (§7.6.1, CN:4564) that a motor used below 38.4 V (80 % of the 48 V default) must have `2000:07` set to its real supply before shipping.
- **Restore on a pack:** `2000:07` accepts 20 V at run time (below the documented 24–48 range), which moves the limit to 16 V; a restore at 18.2 V then worked, and the restore itself set `2000:07` back to 24. The console does this before every restore on a supply below the limit. It doesn't help a save, which would store the lowered value; saving stays a 19.2 V-and-up operation.
- `0xFF02` is "parameter saving fault" (参数保存故障) in the Chinese manual; the English "unexecutable commands" is a mistranslation. While the warning is active, a second refused write sends no new EMCY, so the console reads `603F` after writing instead of waiting for one.
- **A successful flash write** makes the drive unresponsive for roughly 100–200 ms and raise `0x7501` (communication failure) on its own; after a restore it comes back in Ready To Switch On with `603F` = `0x7501`, like a power-up. Harmless.
- **A restore takes effect at once**, not only after the power cycle: the drive reloads its factory settings into the running drive, including the run-time values the console set (undervoltage limit back to 20 V, heartbeat `1017` back to 0, rated voltage back to 24 V). The console applies its session settings again right after the restore. Settings that only load at start-up ("restart activate", e.g. node ID and bitrate) still need a power cycle.
- **Save persists** the manufacturer groups (`2000`, `2001`, `2008`), the CiA 402 group (`6065`) and the node ID and bitrate (`200C:02`/`:03`), on 24 V and on 18.2 V (Parameters plan PAR-005 and PAR-008, 2026-10-12; open question 12). One save at 18.2 V lost `6065` once; not reproduced.
- **Restore leaves** the alarm history `1003`, the node ID (1), the bitrate (code 8) and `200C:01` (2) as they were.
- **Factory values of this firmware differ from the manual** in two places: undervoltage limit `2001:10` = **20 V** (manual 24) and communication-failure reaction `200E:09` = **`0x03017501`**, halt and hold (manual `0x00017501`, fault). Both bench motors already had these. The full factory dump is in NiMotion factory dump, for the Parameters plan (M6).
- **Never** use the NiMotion-internal codes `0x9A899E87`/`0x9A899E80` or the `0A`/`0B` sub-indices. **Never** write `200C:01`.

## Parameters and tuning (bench, 2026-10-12)

Test motor 141416, Parameters plan (M6).

- **The parameter table** covers 446 documented objects of the 1048 the drive answers (PAR-001); the rest are left out of the console (open question Q4 of the plan).
- **"When stopped" writes while running** (Q3) were not tried: the console never sends one.
- **Rated voltage `2000:07`** (Q2) is used for the flash-write check; whether it does more was not compared. A save on the pack stores 20 V, and the PTU firmware never rewrites it, so a console-configured motor would run in a 24 V PTU with 20 V. Revisit before the console configures motors for PTUs.
- **The drive caps the profile ramps silently:** `6083` is held to max acceleration `60C5` and `6084` to max deceleration `60C6` without an abort (factory 500 000 and 1 000 000 counts/s², 3000 and 6000 rpm/s). The step test raises them for its ramp.
- **Gains:** speed loop gain `2008:01` factory 30.0 Hz, integral time `2008:02` 8.00 ms, position loop gain `2008:03` 1500. All apply immediately while running; at about 7 Hz the speed visibly oscillated at 600 rpm. The PTU tunes only the first two, per motor type.
- **Torque-limiting protection `0x8311`** (BLM57 manual) came about 2 s into a held stall with the stall reaction set to warning; a self-resetting warning, the motor kept running.

## Profile torque mode (bench, 2026-10-12)

Test motor 141416, 24 V supply, TRQ-001 in the Torque mode plan. Chinese manual §6.6 (CN:1822).

- **It runs on firmware V0.2.5.a5:** `2002:01 = 0`, `6087` slope, `6071 = 0`, `6060 = 4`, the usual state walk, then the target. Statusword `0x0237` in Operation enabled; bit 10 (target reached) stayed 0 throughout.
- **`6071` is % of the rated current** (0.1 % units): 10 % held 0.17 A and 30 % held 0.50 A, against `2000:09` = 1.7 A. Negative targets push the other way (felt by hand).
- **`6077` (actual torque) is on another scale and has no sign:** 0.8 % at 10 %, 2.3–2.4 % at 30 %, +0.8 % at −10 %, 0.3 % free-running. About 7.8 % of the target; what 100 % stands for is unknown (perhaps the drive's peak current, unverified).
- **The current reading `6078` had no sign** at −10 % either (+0.16 A); it has none in velocity mode either (see the current section).
- **Free shaft at 10 %:** runs up to exactly the speed limit `2007:10` (3000 rpm, factory) and holds it at 0.07 A, without an alarm or visible hunting in 1 s samples.
- **Torque to 0, then power off** leaves the shaft coasting from speed; nothing brakes it.
- **No stall at the rated current:** held at 100 % (1.7 A) for about 60 s, no `0x2312`. The stall check needs more than the rated current (Chinese manual §9.3), which `6071` within ±100 % never asks for.
- **Full torque needs supply headroom:** at 100 % the drive raised undervoltage `0x3220` twice on a bench PSU limited to 2 A (the voltage dipped for a moment, not visible on the PSU's display); at 3 A no trip. Reaction `0x01013220`: fault with a slow ramp, self-resetting, so the drive drops to Switch on disabled without staying in Fault. A Makita pack has the headroom; a bench PSU wants 3 A or more.
- **Friction balance:** at 4-5 % a free shaft settles at about 1700 rpm instead of running to the limit.

## A blank parameter flash: "Bad parameter" that survives a restore (bench, 2026-10-15)

Motor 206233, BLM42 hardware "42102605", firmware **V3.2** (the test motor 141416 runs V0.2.5.a5). It came with "Bad parameter" (`0x6320`, parameter setting error) that a restore defaults from the console did not clear. Recovered with the `sdo`, `reset` and `restore` serial commands and `tools/recover-parameters.py`; every step below is from the log.

- **What it looked like:** statusword `0xFF3F` (Fault reaction active with all manufacturer bits set), `603F` = `0x6320`, error register `0xC1`. The alarm history held the same four entries over and over, newest first `0x0401FF02` (save fault), `0x00016320` (parameter error), `0x04017308`, `0x04013220`: one group per power-up, with the save fault from each restore attempt.
- **The cause: the parameter flash was blank.** A `dump` read all ones in nearly every stored object: `2000`–`2006` and the whole CiA 402 group were `0xFFFF`/`0xFFFFFFFF`/`0xFF` (rated current, pole pairs, encoder bits, DI functions, modes, limits, ramps…), `1017`, `100C`, `100D`, `1014` (EMCY disabled, `0x80000000`), the PDO COB-IDs and the product code `1018:02` too. `2008`, `2009`, `200C` and `200E` still held values (`2008:01` 200 Hz, `2008:02` 1 ms, `200E:02/:04/:06` as the PTU firmware sets them), so part of the area survived. The manual's description of `0x6320` fits: DI1–DI3 and the virtual inputs all had function number `0xFFFF`, the same function on different terminals (CN:5195). `0x7308` is "no encoder calibration data in flash", raised when `2000:20` (the calibration function bit, factory 0) is not 0 and the area is empty (CN:5267); it was `0xFFFF`.
- **Why the restore never took:** the drive refused every flash write with `0xFF02` (parameter saving fault) while its parameters were invalid, at 19.5 V with `2000:07` lowered to 20 V, so the voltage guard was not the reason. `1011:01` read **2** after the failed restores and **1** after a successful one, which is the manual's "non-1 means unsuccessful" (CN:4578): a cheap check the console could show. In this state the drive also refused the heartbeat write `1017` with abort `0x06010000` (unsupported access), so the console's pulled-cable stop (D-023) was not armed.
- **Recovery:** write every blank object its value from a factory parameter dump at run time (338 writes in 50 s, 10 refused: `2005:03` is 32-bit on this firmware, the rest read-only), which cleared `0x6320` on its own and left the drive in Switch On Disabled with the save-fault warning still showing. A fault reset, then `1010:01` = save: accepted, `1010:01` read 1, `603F` went to `0x7501` for the usual post-write blip and the history got `0x03017501` instead of another `0xFF02`. Then the console's restore defaults worked (`2008:01` back to 300, `2000:07` to 24 V, `1011:01` = 1), the fault reset left `0x0260`, and `1017` became writable again.
- **Verified after a power cycle (same day):** the factory values held (`2000:09` 170, `2000:11` 4, `2000:19` 18, `2008:01` 300, `2000:07` 24 V, DI functions 0, `2000:20` 0), the boot added only the usual undervoltage entry (factory limit 20 V on a 19.4 V supply, lowered to 12 V by the console at connect) and no `0x7308` or `0x6320`, error register 0, and the motor ran at 600 rpm in velocity mode with statusword `0x0637`. NMT reset node `0x81` had shown no effect on this drive, so a power cycle is the only reload test. How the flash became blank is unknown: a save interrupted by power loss is the obvious guess, and the drive did not raise `0x5543` (flash user area without parameters), which the manual reserves for this case.
- **V3.2 differences seen:** statusword manufacturer bits 8–15 are set (`0xFB..`/`0xFF..`) in the fault states, the current-reading object the console polls showed 3.27 A while the parameters were blank (an offset, gone after the rewrite), and `1011:01` reports the last restore's outcome.
- **In the console since 2026-10-15 (D-031, Blank flash recovery):** the run checks read four sentinels, `2000:09`, `2000:11`, `2000:19` and `2003:03`, and call the flash blank when all four read all ones; the Faults screen then shows RECOVER, which does the above in one confirmed sequence. The restore and save read `1011:01` / `1010:01` afterwards and report a refusal from the drive's own answer. `fake blank` on the serial console exercises the UI on a healthy motor (RECOVER then writes nothing, resets and restores); `blank` writes all ones at run time to every entry RECOVER covers, which the drive accepts (325 of 326 on motor 206233) and answers with `0x6320` on its own, plus one EMCY `0xFFFF` (unknown code, error register `0x81`), so the real write path can be tested; nothing is saved and a power cycle undoes it. The console refuses motion while a flash reads blank.

## A second blank flash: the undocumented objects matter (bench, 2026-10-15)

Motor 159709, BLM4205B (rated current `2000:09` = 2.77 A after its restore), hardware "42102605", firmware V3.2. Same blank-flash picture as motor 206233, but the first RECOVER build didn't get through: "the fault didn't clear (0x7308) after 0 objects were rewritten".

- **Why:** RECOVER then wrote only the parameter table's 446 documented objects. The flash takes the undocumented ones with it, and among them is `2000:20`, the encoder calibration bit: `0xFFFF` there means "calibration enabled", and with no calibration data the drive holds `0x7308` as a fault (statusword `0xFF3F`, fault reaction active). Its history also showed three `0x7310` (overspeed) entries, presumably from running with blank constants. Writing the undocumented blank objects from the factory dump (`tools/recover-parameters.py --all`, 153 writes) cleared `0x7308` on its own; a fault reset and the restore then worked, `1011:01` = 1.
- **The factory area was intact**, unlike the first guess: the restore brought this motor's own values back (rated 2.77 A, max 5.5 A, 15 mN·m rated torque, stator 586 mΩ, initial angle `2000:1E` = `0x5C8E` against the BLM42's `0x83D4`). So the console's table values are only ever a bridge to the restore.
- **RECOVER now writes from the whole factory dump** (`protocol/FactoryDump`, 888 objects from `2000h` up, generated by `tools/convert-factory-dump.mjs`), with the width from the table where it knows the object and from the all-ones pattern where it doesn't. Checked on this motor with the `blank` test: 704 objects set to all ones (53 refused as out of range), RECOVER wrote 697 back, `0x6320` cleared on the reset, restore succeeded, and the dump equals the earlier restored state.
- **The restore leaves the communication objects alone:** `1014` (EMCY COB-ID) stays `0x80000000` (disabled), the PDO COB-IDs stay disabled and `1017` stays as it was. A recovered motor therefore sends no EMCY until someone writes `1014` = `0x81`; the console still sees alarms through `603F`. `1017` refuses writes (`0x06010000`) right after a restore on this firmware and accepts them later; not understood.
- **Current reading while blank:** 34.7 A with the power stage off, the same offset artefact as on 206233.

## Offline identification on motor 159709, and its encoder (bench, 2026-10-15)

NiMotion's procedure from the serial console: `2017:01` = 1 (VDI1 = servo enable), `2017:02` = 0, `2000:1E` = 0, `2002:01` = 5, then `2031:01` = 1. The drive went Switched on at the mode change and Operation enabled at the virtual enable; the quick-stop option had been left at 0 (coast).

- **Stage 1 (locked shaft) worked:** `2009:06` = 2 within a second; the drive wrote `2000:1E` = 22569 (stored value 23694, so 6° electrical off), `2000:12` = 604 mΩ (stored 586), `2000:13`/`:14` = 111 (stored 107). Plausible numbers for the motor's own constants.
- **Stage 2 (forward and reverse at 20–80 % rated) failed:** `606C` read −1554 rpm in a 1 s poll, then overspeed `0x7310` (threshold `200A:06` = 3600 rpm) and overcurrent `0x2300`; `2000:16` (Kt) stayed 0, `2009:06` stayed 2. The fault reset (controlword 0x80) cleared it and left the drive Switched on in mode 5.
- **Run 2** (angle left at 22569, `2009:06` still 2) failed at once with `0xFF01`: the procedure wants `2000:1E` = 0 each time, as NiMotion's script does.
- **Run 3** (angle to 0 again, 0.3 s polling): about 4 s of lock, then the spin stage went 0, −251, −1089, −1583, −2611, −3573 rpm in 1.5 s and tripped overspeed and overcurrent. A clean monotonic acceleration under full torque, the signature of a loop with its feedback sign wrong, not of a wrong angle offset (a bad offset cogs or stalls). The angle came out 22767, consistent with 22569 and the stored 23694, so the offset is right and the direction convention is not.
- **Open-loop mode** (`2002:01` = 4, `2001:1D` = 100, `2006:04` = 100, enabled through VDI1 as in NiMotion's script): Operation enabled, but no motion and no current. How that mode takes its speed is not documented.
- **Torque mode from the console** (`torque 50`, `torque -100`, speed limits `2007:10`/`:11` lowered to 500 rpm) after setting `2002:01` back to 0: Operation enabled, no motion, `6078` = 0.01 A, where the healthy motor spins up at 10 %. Either the drive was left in a state the NiMotion-mode switching confused, or the current loop no longer acts; a power cycle was the next step.
- **After a motor power cycle, torque mode worked:** with the restored factory values back (angle 23694, nothing saved in between) and `2007:10`/`:11` lowered to 500 rpm, `torque 100` ran +455, +499, +500 rpm and held exactly at the limit at 0.22 A; `torque -100` the same the other way. Torque sign and encoder sign agree, and the speed limiter, a speed loop, holds its target. So the drive commutates this motor correctly once it has booted on the restored values; the runaway and the failed identification runs happened on a drive that had been recovered and restored at run time without a power cycle since. Which setting the boot makes consistent is unknown ("restart activate" settings load only at start-up).
- **Velocity mode after the power cycle, from the display:** 60 rpm held at 59–62 rpm, ramp stop clean, Switch on disabled. **Motor 159709 is recovered** on its restored factory values; nothing further was saved (the restore had written the flash). Gains and fault reactions are factory, not the PTU's; at the factory 30 Hz / 8 ms the 60 rpm run was stable but shaky with a noisy current, and the PTU's BLM4205B values (speed loop 110.0 Hz, integral time 2.00 ms) made it smooth. The factory gains are too soft for this motor; a save would keep them.
- **Conclusion for the console:** a drive that was recovered and restored at run time can be inconsistent until it boots again (settings marked "restart activate" load only at start-up, and the undocumented objects RECOVER writes may be among them). The console now refuses motion after a RECOVER that wrote values until the motor has been power-cycled and found again. The first motor (206233) ran after its recovery without a cycle, so this is not deterministic; the refusal costs one unplug.
- **Hand-turn test, stage idle:** `6064`/`606C` sampled every 0.25 s while the shaft was moved by hand in steps back and forth; the position and the velocity chart followed the hand faithfully (about 4000 counts per half second at hand speed, sign with the direction). **The encoder is fine.** What is left is the loop: a speed controller that holds full torque (`0x8311`) while the reported speed is above its target applies torque in the wrong direction, so the suspect is the commutation direction (electrical angle against encoder direction), not the encoder.

## A weak motor: initial angle 81° off (bench, 2026-10-15)

Motor 159687, BLM4205B, V3.2, no faults, PTU gains (1100/200), history: two overcurrent `0x2300`. It ran in velocity and position mode but could be stopped by hand while the console showed 2.77 A, i.e. the drive at its torque limit (`6072` = 100 % of the 2.77 A rating).

- **The offline identification completed** (NiMotion's procedure, `2000:1E` = 0, mode 5, VDI1 enable): about 4 s of lock, a few seconds of forward and reverse up to about 1460 rpm, `2009:06` = 3, no fault. Result: initial angle `2000:1E` = **47318** against the stored 32524, 14794 units = **81° electrical off**; resistance 575 mΩ (stored 568), inductance 107 (stored 104), Kt 4 (same), inertia 6 (stored 4). With cos 81° ≈ 0.16, the motor had a sixth of its torque per amp, which is the weakness. Where the wrong angle came from is unknown; the other BLM4205B (159709) had its stored angle within 6° of the identified one.
- **With the identified angle in RAM**, torque mode at 30 % ran to the 500 rpm limit at 0.45–0.57 A; 60 rpm by hand still felt weak. **Saved** (user area, `1010:01` = save, confirmed) and **power-cycled: still weak** at 60 rpm. So the angle alone was not it, or the identified value isn't what the drive uses. - **Instrumented hand stall at 60 rpm** (0.1 s polling): phase RMS currents `200B:12`–`:14` read 232/233/234 (balanced; the unit is probably 0.01 A), `6078` = 999 per mille, bus 18.5 → 18.2 V, and the drive raised stall `0x2312` within about a second (the BLM4203B test motor takes about 10 s), after which the console switched the stage off. Real, balanced current and little torque, on two angles 81° apart: the angle is not the limiter. What is left is torque per amp itself, i.e. a partly demagnetized rotor (two `0x2300` overcurrents in its history), which the identification's whole-unit Kt (4 on both BLM4205Bs) cannot resolve. Reference needed: the same hand stall on the good 159709.
- **A factory reset from the display** (run time, no power cycle) then produced the same runaway as on 159709 (2690 rpm, STOP useless) **with full torque**, which clears the magnets; stopped by a power cycle. After the cycle, the saved angle 47318 was confirmed in `2000:1E` (the user-area save does persist it), and with the stall reaction set to warning at run time (`200E:02` = `0x04002312`) the hand stall no longer tripped and the motor was still weak. So torque at 2.77 A is genuinely low.
- **The angle is not what it seems:** identifications after the stalls read 47318, 61956 and 14703, each about +80° electrical on the previous `2000:1E`. Read as residuals, every run measured the same ≈80° error on top of whatever `2000:1E` held and wrote old + 80° back; had the written value been in effect, the next residual would have been near zero, as on the healthy 159709 (22569, then 22767). So **on this motor the booted drive does not take its offset from `2000:1E`.** A mechanical slip was considered and rejected: in the run-time runaway the same rotor delivered full torque, then after a boot with no stall in between it was weak. The offset source that differs between the two motors is the encoder calibration area behind `0x7308` (blank on 159709, intact and apparently 80° wrong on 159687), which `2000:1E` and the user-area save don't reach; NiMotion's procedure ends with the manufacturer-area save for that reason. **The calibration bit was tried:** writing `2000:20` = 1 made the drive raise a save fault `0xFF02` at once and then `0x7308` (no calibration data), so this motor's calibration area is empty too; the identification with the bit set locked and measured 46956 (consistent with the first run's 47318), then failed the spin stage with `0xFF01` while `0x7308` stayed. The bit set back to 0, fault reset, 47318 left in RAM (flash holds 47318 too). **Conclusion:** the booted drive on this motor commutates from a source `2000:1E` doesn't reach and the console can't see; what is left is NimStudio with NiMotion's manufacturer-area procedure, or NiMotion. Both BLM4205Bs run firmware V3.2, so the difference between them isn't the firmware.

## The identification as a diagnostic: residuals (bench, 2026-10-16)

With the console's `calibrate` command (Motor calibration plan, CAL-001; the same sequence as NiMotion's kit) on a 24 V supply:

| Motor | Type, firmware | Stored `2000:1E` | Identified | Residual | Runs |
|---|---|---|---|---|---|
| 205571 | BLM4203B, V0.2.5.a5 | 6284 | 4748 | −8° | good |
| 159709 | BLM4205B, V3.2 | 23694 | 22569, 22767 | −6°, −5° | good after its recovery |
| 144701 | BLM4203B, V0.2.5.a5 | 23516 | 37422 | +76° | weak |
| 159687 | BLM4205B, V3.2 | 32524 | 47318, 61956, 14703 | +81° each time | weak |

Good motors come back within 10° of their stored angle; the weak ones about 80° away, and 159687 again 80° away from whatever was written, i.e. its booted drive doesn't commutate from `2000:1E` at all. R and L come back within 1 % of the stored values on every motor. The run takes 9–10 s on V0.2.5.a5 (stage 2 after 0.1 s, stage 3 after 7.5 s, the drive stays in Operation enabled with the shaft still) and 15–20 s on V3.2 (the drive leaves Operation enabled by itself at the end). Whether writing the identified angle back fixes a weak BLM4203B (144701) is untested; on the V3.2 motor it didn't.

## Runaway after a recovery, and the stop that worked (bench, 2026-10-15)

Motor 159709 (BLM4205B) after its recovery and restore, asked for 600 rpm in velocity mode: it ran up to about 2090 rpm at 2.1 A, raised overload `0x2311` and torque-limiting `0x8311` (both warnings, so RESET FAULT stayed disabled), and **STOP did not stop it**: the drive sat in Quick stop active (`0x0297`) at 2090 rpm, braking on its ramp through the same loop that was running away. Controlword 0 (disable voltage) in that state was accepted and ignored.

- **What stopped it:** quick-stop option `605A` = 0 (disable the drive function, i.e. coast) while in Quick stop active. The drive went to Switch on disabled at once and the motor coasted down. The console's STOP uses the factory `605A` = 2 (ramp, then power off); for a loop that has lost control the ramp is useless and coast is the only bus-side stop. A pulled cable would not have helped either: `1017` refused the heartbeat write after the restore.
- **Likely cause:** wrong commutation, i.e. the initial angle `2000:1E` or the encoder direction, which makes the torque the drive applies feed the speed instead of fighting it. The values came from this motor's own restore (`2000:1E` = `0x5C8E`), so either its factory area holds a wrong angle, or the lost encoder calibration data (`0x7308`) was needed on this motor after all. The offline parameter identification (control mode 5, Chinese manual §8.2) can find the angle again when `2000:1E` is 0 first; not tried yet.

