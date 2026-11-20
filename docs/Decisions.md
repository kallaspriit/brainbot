# Decisions

A log of decisions and why they were made. Newest last.

**D-001 to D-037 come from the NiMotion debug console** this firmware started from (a handheld bench tool on the same board, October 2026). They describe the code as it came over; some don't apply to Sparky (one motor at a time, PTU references, the latency tool) and will be superseded as the code is cleaned up. Sparky's own decisions start at D-038.

**Status values:** _Accepted_ — agreed and in effect. _Proposed_ — our position, waiting on evidence or confirmation. _Superseded_ — replaced by a later entry.

When an [open question](Open%20questions.md) is answered, record the answer here and remove it from the open questions.

---

## D-001 Waveshare ESP32-P4-WIFI6-Touch-LCD-7B as the platform

- **Date:** 2026-10-08
- **Status:** Accepted
- **Context:** The console needs a large touch display, CAN and enough compute for live plots. The board was already at hand.
- **Decision:** Build on the Waveshare ESP32-P4-WIFI6-Touch-LCD-7B, in a custom 3D-printed case.
- **Consequences:** One CAN transceiver on board, non-isolated. The board takes only 5 V (D-003). Firmware must match the P4 chip revision of the board.

## D-002 Bare motors, one at a time; PTU behaviour not matched

- **Date:** 2026-10-08
- **Status:** Accepted
- **Context:** Main uses are testing velocity and position mode, troubleshooting, factory reset and tuning.
- **Decision:** The console works on bare motors, one at a time, on its own CAN port and power output. It does not need to reproduce PTU behaviour.
- **Consequences:** One CAN channel is enough, no isolation, no second transceiver. Node ID 1 on both PTU axes is no problem. Motors run on raw pack voltage (D-005).

## D-003 Makita 18V LXT pack powers both the board and the motor

- **Date:** 2026-10-08
- **Status:** Accepted
- **Context:** A self-contained unit. The board accepts only 5 V; the motors need 12–36 V (BLM42) or 16–52 V (BLM57).
- **Decision:** One Makita 18V pack. A buck converter makes 5 V for the board; the motor takes pack voltage through a switched rail.
- **Consequences:** Need fuse, reverse-polarity protection and TVS at the pack input, a 5 V buck, a motor rail switch and our own undervoltage cutoff. (The motor rail switch was dropped in D-013.)

## D-004 Board powered through the RS485 connector

- **Date:** 2026-10-08
- **Status:** Proposed
- **Context:** Feeding a USB-C port is awkward in a self-contained case. The RS485 connector is unused, and its 5V pin is the `Core_5V` rail directly (schematic).
- **Decision:** Feed 5.0–5.1 V from the buck into the RS485 connector's 5V and GND pins, with a 5 V TVS at the connector. Leave J4 empty.
- **Consequences:** The board's ON/OFF switch no longer works; the main switch goes on the battery side. USB-C stays usable for flashing (ideal diodes block back-feed). `Core_5V` has no protection of its own, so the buck must not overshoot. Connector warmth to be checked. Alternative if needed: solder to `VCC_5V` to keep the onboard switch.

## D-005 Motor runs on raw pack voltage, cutoff at about 16.5 V

- **Date:** 2026-10-08
- **Status:** Accepted
- **Context:** A 5S pack gives 15–21 V. BLM42 motors are fine across that range; the BLM5709B needs 16 V minimum. A boost to 24 V would add a 100–150 W converter for no benefit to the use cases (D-002).
- **Decision:** No boost. The console quick-stops the motor and refuses motion below about 16.5 V (originally: switched the motor rail off; there is no switch since D-013).
- **Consequences:** Top speed is lower than rated (roughly a quarter, unmeasured). The cutoff also protects the pack, since Makita packs rely partly on the tool for that. **Added 2026-10-08:** the drive's own bus undervoltage limit (`2001:10`) is 24 V from the factory, so a motor that has not been through the PTU setup (which writes 12 V) will not enable on the pack. The console reads the limit on connect and, when it is above 12 V, lowers it to 12 V automatically for the session (run-time write, not saved, reverts at the motor's power-off), rather than adding a 24 V boost. The value the motor came with is shown, since a wrong value is a finding. This is the one exception to the "writes are explicit" safety rule in `CLAUDE.md`. A first version put it behind a button; dropped the same day because the motor can't run on the pack without it.

## D-006 Console controls motor power

- **Date:** 2026-10-08
- **Status:** Superseded by D-013 — no switched motor power
- **Context:** Saving parameters, factory reset and node ID or bitrate changes take effect only after a power cycle. A stop that does not depend on the drive answering is also wanted.
- **Decision:** The motor rail is switched by the firmware, with current sensing.
- **Consequences:** Parts (eFuse, high-side switch, current monitor) still to choose. The UI offers power-cycle as part of save, factory reset and node changes.

## D-007 Native ESP-IDF 5.5.5, not PlatformIO or Arduino

- **Date:** 2026-10-08
- **Status:** Accepted
- **Context:** PlatformIO was the preferred tool. Its official espressif32 platform does not support the ESP32-P4; the community pioarduino fork does (55.03.312-1, ESP-IDF 5.5.5). Tried on 2026-10-08: its ESP-IDF builder drops the `-include` flag that Waveshare's `esp_lvgl_adapter` adds, so the build fails, and a post-build script did not reach the flags. Waveshare's examples, the BSP, LVGL and `esp_hosted` all target native ESP-IDF.
- **Decision:** Native ESP-IDF v5.5.5 (installed with EIM to `C:\esp`), built with `idf.py`, in C++ (`gnu++2b`). VS Code with the Espressif ESP-IDF extension and the C/C++ extension pack as the IDE. Waveshare BSP `3.0.1` and LVGL `9.5.0` pinned in `main/idf_component.yml`.
- **Consequences:** No PlatformIO. ESP-IDF does not run under Git Bash (MSYS); use PowerShell with the EIM activation script. Moving to ESP-IDF 6.x is a deliberate later step (Waveshare also validates 6.0.2).

## D-008 STOP is a CiA 402 quick stop

- **Date:** 2026-10-08
- **Status:** Accepted
- **Context:** STOP has to work from any screen, in any mode, including mid-sequence. Navigation shell plan, D1.
- **Decision:** STOP writes controlword `0x02` (quick stop). The drive brakes with `6085` and, per `605A`, ends with the power stage off. If the drive turns out to stay in Quick Stop Active, a disable-voltage write follows at standstill.
- **Consequences:** One SDO write, independent of polling. `6085`, `605A` are read and shown. There is no switched motor power (D-013); unplugging is the hard stop.

## D-009 One motor service owns the bus; screens send commands

- **Date:** 2026-10-08
- **Status:** Accepted
- **Context:** Open question 2 (module structure). Navigation shell plan, D2.
- **Decision:** A `MotorService` task is the only code that talks CAN. Screens send it commands through a queue and read one status snapshot; STOP jumps the queue and cancels sequences. LVGL code never waits on the bus. EatSpot's `App`/`AppModule` container is not carried over; services are plain classes wired in `main.cpp`.
- **Consequences:** Polling, state walks and safety logic live in one place. Screens stay thin and testable on the device.

## D-010 Leaving a motion screen stops the motor

- **Date:** 2026-10-08
- **Status:** Superseded by D-016 — the motor keeps running across screens
- **Context:** Navigation shell plan, D4.
- **Decision:** Leaving Velocity or Position sends a quick stop; a motor only runs while its controls are on screen.
- **Consequences:** A "keep running" toggle can be added later if a test needs the motor running while another screen is open.

## D-011 Stopping the motor when the console loses it

- **Date:** 2026-10-08
- **Status:** Accepted (revised twice the same day: after the bench test, and when switched motor power was dropped in D-013); the pulled-cable gap closed by D-023
- **Context:** If the CAN cable comes off or the console hangs or reboots while the motor runs, the drive keeps its last setpoint. The drive's communication watchdog (`200C:1B`) did not arm at run time on the bench (NAV-001), and saving it is not acceptable because the motor would go back into a PTU that never feeds it. Navigation shell plan, D7.
- **Decision:** Console hang: the ESP-IDF task watchdog resets the console. Console reset or reboot: the connect sequence stops a drive left running. Pulled CAN cable: accepted gap; the motor runs until the cable is back, when the connect sequence stops it.
- **Consequences:** Fine for bare motors on a bench (D-013). The drive's watchdog is read, and fed if a drive has it on, but never written.

## D-012 Code-first UI with a serial screenshot command

- **Date:** 2026-10-08
- **Status:** Accepted
- **Context:** Good UX and design matter; the UI is written by a developer and an AI agent working in text. Navigation shell plan, D5 and D6.
- **Decision:** Theme tokens and a small C++ component set on LVGL 9 (details in [UI design](UI%20design.md)); no visual designer, no mockups. A `screenshot` serial command captures the display to a PNG on the PC for checking changes.
- **Consequences:** LVGL Pro stays the option to revisit if code-first is too slow.

## D-013 No switched motor power; operator power-cycles the motor

- **Date:** 2026-10-08
- **Status:** Accepted
- **Context:** D-006 planned a firmware-controlled motor power switch, for power-cycling after save, factory reset and node changes, for a hard stop, and for the pulled-cable case. The console is used with bare motors on a bench, where that protection isn't needed, and there will be no custom power board.
- **Decision:** The motor is powered whenever the pack is connected. Workflows that need a power cycle ask the operator to do it ("power-cycle the motor now"), then detect the drive dropping out and coming back, and verify the result. The power wiring is modules: an inline fuse, a 5 V buck module for the board, and optionally a resistor divider for pack voltage.
- **Consequences:** Supersedes D-006. The safety rule "motor power can be cut from the UI" is dropped; STOP in the UI and unplugging are the stops. A pulled CAN cable while the motor runs has no stop until it's plugged back (D-011). Keep the fuse: a shorted Makita pack is a fire risk regardless of motor safety.

## D-018 Mini560 Pro 5V as the board buck

Numbered D-014 by mistake until 2026-10-09, when it collided with the App structure decision; kept in its place in the log.

- **Date:** 2026-10-08
- **Status:** Accepted
- **Context:** D-004 needs 5.0–5.1 V on `Core_5V` without overshoot, from a 15–21 V pack. Two modules on hand were bench-tested on 2026-10-08 (PSU, e-load, DMM at the module output, thermal camera):
  - HobbyWing 3A UBEC (rated 5.5–26 V in, 5 V/6 V jumper): 5.34 V no load, 5.24–5.25 V at 2 A from 10, 15 and 21 V; about 82% efficient. Regulates well but sits about 0.25 V above D-004, and the 6 V jumper can be knocked into place.
  - Mini560 Pro 5V (seller sheet: 7–32 V in, 4 A continuous; IC unmarked, asynchronous with an SS1060 catch diode, although the listing says synchronous): 5.08 V no load, 5.03 V at 2 A from 21 V; about 87% efficient; no measurable standby input current. At 28 V input and 2 A for 2 minutes it kept regulating; hottest spot 76.6 °C (IC and diode area, open air, about 30 °C ambient).
- **Decision:** Use the Mini560 Pro 5V. Keep the UBEC as a spare.
- **Consequences:** Its fixed output removes the jumper risk. The 32 V input rating is the seller's claim; 28 V is measured. The board draws about 0.6 A, so the module runs far cooler than in the 2 A test, but it still needs some air in the case and should not sit against the pack. Still to check: start-up overshoot on a scope (the TVS from D-004 is the safety net until then), and that the board boots reliably from it.

## D-014 App structure: an App class and layer folders

- **Date:** 2026-10-09
- **Status:** Accepted
- **Context:** Before more screens and the motor service, the firmware needed one owner for its parts and a dependency direction. App structure plan, D1, D2, D5, D7, D11.
- **Decision:** An `App` class owns every part as an explicit member and starts them in `begin()`; `main.cpp` only creates it. Folders by layer, dependencies pointing down: `ui/` → `services/` → `protocol/` → `drivers/`, plus `debug/`. `protocol/` reaches the bus through a `CanPort` interface. One screen registry table drives the screen manager, the home grid and the `open <screen>` serial command. Components are functions with an options struct unless they need methods after creation. Done before NAV-005 (segmented SDO).
- **Consequences:** No module registry or RTTI (EatSpot's pattern not carried over). Adding a screen is one class and one registry row. `protocol/` can get PC tests against a fake bus later.

## D-015 State flows to the UI through UiState; commands through services

- **Date:** 2026-10-09
- **Status:** Accepted
- **Context:** The top bar and every screen need motor state, and the motor task and the LVGL task must stay apart. App structure plan, D3, D4, D6, D8, D9, D10.
- **Decision:** Services publish snapshots (`MotorStatus`) under a mutex. One LVGL timer copies them into `UiState`, LVGL subjects grouped by domain, holding integers in the drive's native units (strings only for text). Widgets bind to subjects. Events travel as a value plus a sequence number; lists as service-owned models with a version subject. Screens get a `UiContext` (state, thread-safe service commands, `Navigator`); nothing global. Screens have `onEnter`/`onLeave` hooks and navigation keeps a back stack. Dialogs and toasts are overlays on LVGL's top layer owned by the screen manager.
- **Consequences:** Only the LVGL task touches LVGL; everything else goes through `UiState` or `lv_async_call` under the display lock. Screen-local values stay in the screen, not `UiState`.

## D-016 The motor keeps running across screens

- **Date:** 2026-10-09
- **Status:** Accepted, narrowed by D-022 (entering another motion screen ends the current mode's motion)
- **Context:** D-010 stopped the motor when leaving a motion screen, because nothing else showed that it was running. Since NAV-007 every screen has the top bar with the drive state ("Running" in green) and a STOP that is filled red whenever the drive can move. Stopping on leave got in the way of the main reason to leave: watching the motor on Overview while it runs.
- **Decision:** Leaving a motion screen doesn't stop the motor. The top bar STOP is the stop on every screen. A motion screen starts its controls from the targets the service holds (`MotorStatus::targetVelocityRpm`), so coming back to a running motor never changes its speed.
- **Consequences:** The "stop control on every motion screen" safety rule becomes "on every screen", which the top bar provides. Starting another mode while running will switch modes through the start sequence; the Position plan confirms that. Unplugging stays the hard stop (D-013).

## D-017 Velocity plot from 50 Hz SDO sampling; TPDOs still deferred

- **Date:** 2026-10-09
- **Status:** Accepted
- **Context:** The Navigation shell plan's D3 left live plots out, since smooth plots need TPDOs, which change the drive's PDO mapping. On the Velocity screen a chart of the ramp and of inconsistencies turned out to be worth having now.
- **Decision:** The motor service reads `606C` on its own about every 20 ms between the 10 Hz polls and keeps the samples, with the target the drive follows, in a ring buffer. The UI fetches the new ones when a sample count in `UiState` changes and plots about 10 s. TPDOs stay deferred.
- **Consequences:** Ramps, overshoot, settling and slow ripple are visible; anything faster than about 20 Hz (vibration, commutation ripple) is not, and `606C` is in whole rpm. The extra reads also make STOP react within one sample interval instead of one poll interval. A plot that needs more bandwidth will need the TPDO decision.

## D-019 Motion limits come from the motor: rated speed and the quick-stop deceleration

- **Date:** 2026-10-09
- **Status:** Accepted
- **Context:** The Velocity screen capped speed at a fixed ±300 rpm and acceleration at 2000 rpm/s, chosen as a cautious bench range in the Navigation shell plan. The drive reports what it is built for.
- **Decision:** On connect the motor service reads the rated speed (`2000:0E`) and the max motor speed (`6080`). The speed limit is the rated speed, capped by `6080` and `607F` when they are lower; the console never runs a motor past its rated speed. The acceleration limit is the quick-stop deceleration (`6085`), so a normal ramp is never steeper than STOP. Every request is clamped to them in the service; until they are read the old 300 rpm and a 300 rpm/s fallback apply. Targets can be typed on a number pad as well as set with the slider and steps.
- **Consequences:** On the test BLM42: 3000 rpm and 3000 rpm/s. Running past rated speed is a tuning question for the Parameters plan, not a console default. On an 18 V pack the reachable top speed is lower than rated (unmeasured). Braking from high speed on a bench supply that can't absorb power could trip the drive's overvoltage alarm (`0x3210`); a battery absorbs it.

## D-020 Software position limits are widened automatically for position mode

- **Date:** 2026-10-09
- **Status:** Accepted
- **Context:** `607D` defaults to ±65 535 counts (about ±6.5 turns, Chinese manual object table). Bare motors sit at large absolute positions (the test motor at about 54 million counts), so default limits would make every move raise 0x8613 ("pause and lock the shaft"). Position mode plan, D1.
- **Decision:** When position mode starts, the console widens `607D` to nearly the full int32 range if it is narrower than the move needs. Run-time write only, never saved, gone at the motor's power-off, and shown with the values the motor came with.
- **Consequences:** A second automatic run-time write next to the undervoltage limit (D-005), added to the exception in the "writes are explicit" safety rule; a save screen must list it among the changes it would persist. The motor is bench-tested only, so the PTU's limits don't apply.

## D-021 Positions are shown in turns from a console zero kept per motor

- **Date:** 2026-10-09
- **Status:** Accepted, partly built: turns from a zero taken at every connect (2026-10-11); "Zero here" is the ZERO button on the Position screen's position tile (2026-10-13). Keeping the zero per serial number is still skipped (POS-005), so a reconnect or a motor power cycle re-zeroes
- **Context:** The drive counts in encoder counts (10 000 per turn by default) from an absolute multi-turn position that means nothing on a bare motor. Position mode plan, D2.
- **Decision:** Position targets and readings are in turns (three decimals) from a console zero. The zero is taken when a motor connects and kept across reconnects of the same serial number; a different motor gets a new one. "Zero here" moves it. Counts per turn come from `608F` and `6091`. The drive always gets absolute counts; raw counts stay visible.
- **Consequences:** Nothing is written to the drive for the zero (no homing). The zero is lost when the console restarts.

## D-022 Entering a motion screen ends the other mode's motion

- **Date:** 2026-10-09
- **Status:** Accepted
- **Context:** D-016 keeps the motor running across screens, but a velocity run and a position move can't both be in control. Position mode plan, D6.
- **Decision:** Opening the Position screen while velocity runs does a smooth stop; opening the Velocity screen while position moves or holds halts and releases it. A message says why. Other screens never stop anything.
- **Consequences:** Narrows D-016: the motor keeps running across non-motion screens such as Overview. The screen on display is the mode in control.

## D-023 The drive's heartbeat stops the motor when the bus is lost

- **Date:** 2026-10-11
- **Status:** Accepted
- **Context:** The communication watchdog `200C:1B` doesn't trip on drive firmware V0.2.5.a5 (bench tests), and the firmware has no heartbeat consumer, abort-connection option or error behaviour objects (`1016`, `6007`, `1029` don't exist). With SDO only, the drive never transmits on its own, so a pulled cable went unnoticed (D-011's accepted gap). A bench test showed a different mechanism works: with the heartbeat producer `1017` on, a heartbeat nobody acknowledges makes the drive raise `0x7501`, and `200E:09 = 0x03017501` (halt on the quick-stop ramp, lock the shaft, self-reset) stops it smoothly, from 60 and 2000 rpm.
- **Decision:** On every connect the console sets `1017` = 100 ms. Run-time write only, never saved, gone at the motor's power-off; Overview shows it with the value the motor came with. The console also reads `200E:09` and warns if its response code (4 warning, 5 ignore) wouldn't stop the motor; it doesn't write it.
- **Consequences:** A second automatic run-time write next to D-005, added to the "writes are explicit" exception in `CLAUDE.md`; a save screen must list it. A pulled cable now stops the motor within about the heartbeat interval. It only works while the console is the drive's only partner on the bus: any other node that acknowledges the heartbeat hides the loss (a point for the PTU, which has the same gap). On reconnect the drive is found enabled and holding; the connect sequence switches it off.

## D-024 Faults are kept as evidence; START and GO still reset them

- **Date:** 2026-10-11
- **Status:** Accepted
- **Context:** A motor on the bench usually comes from a PTU with a problem, and its alarm history `1003` is the only record of what happened. START and GO reset a fault on their own, which is convenient on a bench but could hide what was there. Faults plan, D2, D3, D8.
- **Decision:** On connect the console reads the alarm history before it writes anything to the drive. It keeps a session log of every EMCY with console time and the motion at that moment, including faults that START or GO reset. The automatic reset stays.
- **Consequences:** The connect sequence orders its reads before its run-time writes (D-005, D-023). The log lives in RAM and is lost when the console restarts. **Extended 2026-10-11:** a stall leaves `0x2312` latched in `603F` without a fault, and the next run halts at once, so START and GO also reset when `603F` is non-zero; and a non-zero `603F` in Operation enabled counts as an active alarm (amber in the top bar), since a stall sets neither statusword bit. The console also quick-stops and powers off a run that stalls, since the drive's factory reaction holds at up to the rated current and the PTU's only warns.

## D-025 Restore defaults is a confirmed command on the Faults screen

- **Date:** 2026-10-11
- **Status:** Accepted
- **Context:** The usual "bricked" motor is a misconfigured one. Restoring factory defaults is the quickest way to tell configuration from hardware, and UC-8 needs it anyway. Faults plan, D5.
- **Decision:** A RESTORE DEFAULTS button on the Faults screen, refused while the motor runs or holds, behind a confirm dialog that says everything goes back to factory, that the motor must be unplugged and replugged, and that a motor going back into a PTU needs `setup pantilt` there. The console writes `1011:01 = 0x64616F6C` and nothing else, then checks after the reconnect that `2001:10` reads its factory value. Parameters (M6) will call the same command.
- **Consequences:** The first drive write behind a confirm dialog; the dialog becomes a shared component. **Found 2026-10-11:** the drive refuses flash writes with the bus below about 80 % of its rated voltage `2000:07` (19.2 V at 24; save fault `0xFF02`). Before a restore on a lower supply the console sets `2000:07` = 20 V at run time, which the restore then reloads to factory; it isn't listed in the dialog, since nothing of it remains. The console reads `603F` right after the write and reports a refusal at once; the restore applies at once and resets the console's run-time settings too (heartbeat included), so the console applies them again straight after; the power cycle is only needed for settings that load at start-up.

## D-026 The console finds a motor at another node ID or bitrate

- **Date:** 2026-10-11
- **Status:** Accepted
- **Context:** PTU motors are node 1 at 1 Mbit/s, so anything else was set by hand, and today such a motor shows as "no motor". Open question 14. Faults plan, D6.
- **Decision:** When node 1 at 1 Mbit/s doesn't answer, the console tries node 1 at the drive's other bitrates, then node IDs 2–127 at 1 Mbit/s, and repeats while nothing answers. A motor found elsewhere is used where it is for the session; the console doesn't change its node ID or bitrate (M6).
- **Consequences:** The SDO client's node ID and the CAN bitrate become run-time settings. The home screen shows the search and what to check meanwhile.
- **Update (2026-10-12):** M6 does change them, from the Setup tab's danger group (PAR-008), and only one of the two away from its default at a time, so this search always finds the motor again. A motor moved off both by another tool still isn't found.

## D-027 Parameter metadata is generated from the Chinese manual

- **Date:** 2026-10-12
- **Status:** Accepted
- **Context:** M6 needs name, unit, scale, range, access and effect timing for about 400 objects; the console showed none of it. Parameters plan, D1.
- **Decision:** A tool script parses the Chinese manual's object dictionary (CN:5722 onward) into a committed C++ table. Factory values come from `the factory dump (not kept)`, not the manual, which is wrong in places. Entries the drive has but the manual doesn't describe are left out (2026-10-12), as are record entry counts; the serial `dump` command still reads everything.
- **Consequences:** One table serves the Parameters, Setup and Tuning tabs. The generator needs a review pass; a wrong scale would show and write wrong values.

## D-028 Edits are written at once; saving puts the console's session values back where the bus allows

- **Date:** 2026-10-12
- **Status:** Accepted
- **Context:** `1010:01` saves everything the drive holds, including the console's run-time values (D-005, D-020, D-023, D-025), and flash writes need the bus above about 80 % of `2000:07`. The Chinese manual (CN:4566) says to set `2000:07` to the actual supply and the undervoltage alarm 2 V below it. Parameters plan, D2, D3.
- **Decision:** Parameter edits are run-time writes, collected as unsaved changes until SAVE. The console captures the motor's own values at connect, before writing anything. Before saving it always puts back the heartbeat `1017`, `607D` and the motion values the motion screens write on every start (`2002:01`, `6060`, `6081`, `6083`, `6084`), and puts back `2001:10` and `2000:07` only if the bus is above the original undervoltage limit and 80 % of the original rated voltage; otherwise they are saved as battery values and listed in the confirm. It turns its session values on again after the save. While the motor runs, only parameters that take effect immediately can be edited.
- **Consequences:** A motor saved on a half-empty pack keeps 12 V and 20 V in flash, visible as changed from factory; saving it on the bench supply cleans that up. This extends the safety rule that a save screen lists the run-time values it would persist. **Refined 2026-10-12 (PAR-004):** `2000:07` is an ordinary, editable setting rather than a session value (the console lowers it only during a restore, which reloads it); a save still lowers it for the flash write when the bus needs that.

## D-029 Some parameters are locked in the console

- **Date:** 2026-10-12
- **Status:** Accepted
- **Context:** A few objects can make a motor unusable in ways restore defaults may not fix. Parameters plan, D4.
- **Decision:** Read-only with a reason: `200C:01` CommunicationSelect (safety rules), the save and restore objects `1010`/`1011` (used only by SAVE and RESTORE DEFAULTS), identity, motor electrical constants, encoder calibration and reset (`200D`), brake resistor type `2002:13`, MCU IDs, the CiA 402 command objects (`6040`, `607A`, `60FF`), PDO communication and mapping (`1400`–`1A03`). The console-owned motion values show the running and the motor's own value, read-only. Node ID `200C:02` and bitrate `200C:03` are changed only in Setup's danger group behind a red confirm, followed by a save and a power cycle; the console's search (D-026) then finds the motor.
- **Consequences:** The lock list lives in the generator's input and is reviewed with the table.

## D-030 Raw SDO and NMT serial commands for bench recovery, guarded by the service

- **Date:** 2026-10-15
- **Status:** Accepted
- **Context:** A motor arrived with a blank parameter flash ("Bad parameter" `0x6320` that a restore could not clear, see [NiMotion motors](NiMotion%20motors.md)). Diagnosing and recovering it needed reads and writes of arbitrary objects, a fault reset, a save and a restore from a script, which the UI has no place for and should not have.
- **Decision:** The serial console gets `sdo r|s|w`, `nmt`, `reset`, `restore` and `save` (debug commands, not UI). They go through `MotorService::requestRawSdo` on the motor task like every other command, and the service enforces the safety rules itself: `200C:01` is never written, and `1010`/`1011` take only their documented commands on sub-index 1. `tools/console.py` scripts the console; `tools/recover-parameters.py` rewrites blank objects from the factory dump.
- **Consequences:** Any run-time write is now a serial line away, so the "writes are explicit" rule for the UI stays, and the commands are documented as bench tools. The restore flow could tell a refused restore from a blank-flash drive apart (`1011:01` reads 2 after a failed restore); not done.

## D-031 A blank parameter flash is detected and recovered from the Faults screen

- **Date:** 2026-10-15
- **Status:** Accepted
- **Context:** Motor 206233 came with its parameter flash blank: every stored setting read all ones, "Bad parameter" `0x6320` was active and the drive refused every flash write with `0xFF02` until its parameters were valid, so the console's restore defaults looked like it worked and didn't. Recovered by hand over the serial console (D-030). Blank flash recovery plan, D1–D6.
- **Decision:** At connect the console reads four sentinel objects (`2000:09`, `2000:11`, `2000:19`, `2003:03`); all ones in all four means a blank flash, shown as a run check on Faults with a RECOVER button. RECOVER is one confirmed sequence: every read-write table entry that reads blank gets the table's factory value (locked entries included, D-029 notwithstanding, since they are blank), a fault reset, then the normal restore defaults. Until that restore succeeds the drive counts as "recovered, not restored" and SAVE is refused, so the table's BLM42 constants are never persisted into another model. The restore and save read `1011:01` / `1010:01` afterwards and report a refusal from the drive's own answer, naming the likely cause (blank flash, supply, or unknown). A `fake blank` serial command exercises the UI path on a healthy motor.
- **Consequences:** The recovery is the second place after the restore that writes many objects; both stay behind a confirm dialog, so the "writes are explicit" rule holds. A partly blank area that spares the sentinels is not detected; the `sdo` commands and `tools/recover-parameters.py` remain the fallback. **Extended 2026-10-15:** the recovery writes from the whole factory dump (`protocol/FactoryDump`, generated from `the factory dump (not kept)`), not only the parameter table's documented objects, after motor 159709 stayed in `0x7308` over the undocumented calibration bit `2000:20`. The table still rules access and flags where it knows an object. **Extended again the same day:** after a RECOVER that wrote values, motion is refused until the motor has been power-cycled and found again: motor 159709, recovered and restored at run time, ran away to 2090 rpm asking for 600 and was fine after a reboot (settings marked "restart activate" load only at start-up). Any reconnect clears the wait; a drive that only lost the bus keeps the state it was already running with. Kept as a refusal (decided 2026-10-15); the first motor ran without a cycle, but the one event was a motor STOP could not stop.

## D-032 Motor calibration from the console: the drive's offline identification, saved to the user area, power cycle before motion

- **Date:** 2026-10-16
- **Status:** Accepted
- **Context:** Two BLM4205Bs from the broken-motor bin had commutation offsets that didn't match their rotors; NiMotion's own recalibration procedure runs the drive's offline parameter identification (control mode 5) and saves into the manufacturer area. The identification was run from the serial console on 2026-10-15 and found one motor's angle 6° off (fine) and the other's unreachable. Motor calibration plan, D1–D8.
- **Decision:** A Calibrate tab on Tune & parameters runs the identification: `2000:1E` = 0, mode 5, enable through virtual input 1 as NiMotion's script does, one run per START with the angle residual shown so convergence is visible; STOP ends a run (virtual input off, mode 0, quick stop, coast if the speed doesn't fall); KEEP is the ordinary user-area save (D-028), DISCARD writes the six constants back; every run sets the power-cycle mark of D-031 before motion. The manufacturer-area save code stays forbidden; the tab reports a drive that ignores `2000:1E` instead of pretending to fix it. The six constants' lock (D-029) has the same exception as RECOVER: written only behind the operator's tap.
- **Consequences:** Mode 5 is the first NiMotion-mode use in the console; STOP learns a second path. A kept calibration survives a factory restore only if NiMotion's manufacturer-area step is done elsewhere.

## D-033 The home screen is a 3×3 grid shown with or without a motor; Tune & parameters splits in three

- **Date:** 2026-10-22
- **Status:** Accepted
- **Context:** The latency tool (D-034) needs no motor, but the home cards were hidden while no drive answered, and Tune & parameters had grown four tabs. Latency measurement plan, D1 and D2.
- **Decision:** The home grid is always visible, three rows of three: Velocity, Position, Torque / Tuning, Parameters, Calibrate / Overview, Faults, Latency. Cards whose screen needs a motor are disabled until a drive answers; the search state moves to the top of Overview. Tuning, Parameters (Setup and Parameters tabs) and Calibrate are their own screens, each with the unsaved bar.
- **Consequences:** Reverses D1 of the Motor calibration plan (Calibrate as a tab); the unsaved bar on each screen keeps SAVE and REVERT where the changes are made. The grid is full: a tenth tool needs a second page.
- **Update 2026-11-03:** Rows reordered to follow how a motor is worked on: the drive's state first (Overview, Faults & reset, Parameters), then motion (Velocity, Position, Torque), then tuning and tools (Tuning, Calibrate, Latency). Overview, the card that works without a motor, is top left.

## D-034 Glass-to-glass latency from the console: LED out, light sensor in, continuous ADC timing

- **Date:** 2026-10-22
- **Status:** Accepted
- **Context:** Glass-to-glass video latency needed measuring. The console has free GPIOs, a fast ADC and a screen. Latency measurement plan, D3–D8.
- **Decision:** An LED on P1 GPIO46 in front of the camera and a DFR0026 light sensor on P1 GPIO49 (ADC2 channel 0) on the screen. The ADC runs in continuous mode at 20 kHz; samples get `esp_timer` times from their DMA frame, LED edges at the GPIO write. Cycles are event-driven with random holds so they never lock to the frame rate; every run learns its own dark and bright levels and detects at the midpoint with hysteresis on a 1 ms moving average; a self-test with the sensor at the LED checks the chain. Results: median and p95 of both edges over 100 samples by default (on edges only since D-035).
- **Consequences:** A second service beside `MotorService`, owned by `App`, independent of the motor. No CSV or stored runs until someone needs them.

## D-035 Latency counts only the LED-on edge; the self-test runs 200 fast samples

- **Date:** 2026-11-02
- **Status:** Accepted
- **Context:** D-034 took the median of both edges. The first self-tests showed the edges differ (0.19 ms off, 0.24–0.34 ms on, from the phototransistor's slower turn-on in weak light), and through a camera the off edge mostly adds how fast the monitor darkens. What matters is how long something takes to appear. The self-test held each LED state for 200 ms or more, sized for a camera, so 20 samples took about 10 s.
- **Decision:** A sample is LED on to screen bright. The off edge is still waited for (the next on edge starts from dark, and a missing one is a miss) but not recorded. The self-test takes 200 samples with holds of 5 ms plus a random 0–5 ms, about 10 s; camera runs keep their pacing (hold of max(200 ms, running median) plus a random 0–100 ms).
- **Consequences:** Replaces the "both edges" part of D-034 and of the Latency measurement plan's D8. A camera run of 100 samples takes about twice as long as with both edges. The screen drops the LED on / off row and the second dot colour.

## D-036 Screens that need no motor drop the top bar's search and STOP while no drive answers

- **Date:** 2026-11-03
- **Status:** Accepted
- **Context:** On the Latency screen "Searching for a motor", the CAN icon and a disabled STOP are noise: the tool has nothing to do with a motor. The safety rule asks for a visible STOP on every screen because a motor keeps running across screens (D-016).
- **Decision:** A registry flag (`hidesMotorPart`, the "bare" column) lets a screen hide the search text, CAN icon and STOP while no drive answers. As soon as a drive is connected they come back, so a motor that can run always has its STOP. Latency and the font specimen use it.
- **Consequences:** The safety rule's "STOP on every screen" holds whenever there is a motor to stop. Without a drive, a bare screen's top bar shows only back and the title.

## D-037 LP SRAM stays out of the heap

- **Date:** 2026-11-03
- **Status:** Accepted
- **Context:** After one visit to Parameters, every screen built 2–4× slower until a restart (Overview 138 → 437 ms, the Setup list 290 → 930 ms; timing logs on the bench). Building the Setup list leaves about 23 KB of main internal RAM, and ESP-IDF's default heap then hands out the LP SRAM (`0x5010xxxx`), which is slow and uncached. Long-lived LVGL data reallocated at that moment stays there: the display's event list, read on every layout invalidation, moved from `0x4ffa92ac` to `0x5010a4d8`. With the list rebuilt once per observer on opening, a reopen took over 5 s and tripped the task watchdog (rebuilds fixed in 2492ba5).
- **Decision:** `CONFIG_ESP_SYSTEM_ALLOW_RTC_FAST_MEM_AS_HEAP=n`. When internal RAM runs low, allocations fall back to PSRAM, which goes through the cache.
- **Consequences:** Reopening Parameters builds in about 290 ms again and other screens keep their speed (measured 2026-11-03). Main internal RAM is still tight during the Setup list build; the serial `perf` command logs free heap, per-task CPU and LVGL object and timer counts to watch it.

## D-038 Sparky continues from the console's firmware

**Status:** Accepted (2026-11-20)

- **Context:** The tank test (two NiMotion drives, Xbox controller over BLE) and the OV5647 camera test worked on the console's firmware, on two throwaway branches. Sparky needs both, and the console's CANopen, UI and debug tooling.
- **Decision:** Sparky's firmware starts as a copy of the console's firmware with the camera and gamepad branches merged in (no git history). Tank mode starts at power-on. Hevi branding and the parameter-recovery tools didn't come over; the generated `ParameterTable` and `FactoryDump` stayed in the firmware for now.
- **Consequences:** The firmware still carries console screens (parameters, calibration, latency) that a robot may not need; they go in a later code cleanup. The console's decisions above stay as the record of why the code is the way it is.
