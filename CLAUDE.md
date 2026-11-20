# CLAUDE.md

## Repository overview

Sparky ("BrainBot") is a small tracked robot that gives an LLM a body: a companion and assistant that drives around, sees, hears and talks, and that can reprogram itself (feature requests over a messenger, a coding agent builds and ships firmware over the air). See `docs/Overview.md` for the idea and `docs/Milestones.md` for the plan.

The robot: two NiMotion BLM integrated servos on CAN driving the tracks (left node 2, right node 1 inverted), a Makita 18 V LXT pack, and a Waveshare ESP32-P4-WIFI6-Touch-LCD-7B (7" touch display as the face, ESP32-C6 for Wi-Fi and BLE, MIPI-CSI camera, two microphones, speaker header). The P4 runs no LLM: it handles the wake word, streams audio and video to a server and executes low-level commands; the server decides.

The firmware started as a copy of a NiMotion motor debug console on the same board (D-038), so it still has that console's screens and decisions D-001 to D-037. They go in the codebase cleanup milestone.

This repository is **public** (MIT).

- `/docs` - Development documentation (see `docs/README.md`)
- `/firmware` - ESP32-P4 firmware, native ESP-IDF 5.5.5 project (D-007); sources in `firmware/main/` by layer (see Firmware architecture)
- `/sw`, `/step`, `/bambu` - SolidWorks parts, STEP exports and the Bambu Studio print project
- `/fonts` - Fonts for the printed parts
- `/tools` - PC tools: `screenshot.py`, `console.py`, `convert-fonts.mjs`, `convert-images.mjs`
- `/.claude/skills` - Project skills: `plan-tasks`, `code-review`, `afk`, `yeet`, `summarize-today`

## Commands

Native ESP-IDF v5.5.5, installed with EIM to `C:\esp` (D-007). **Run from PowerShell, not Git Bash** — ESP-IDF refuses to run under MSYS. Activate the environment first in each PowerShell session, then run from `firmware/`:

```powershell
. C:\Espressif\tools\Microsoft.v5.5.5.PowerShell_profile.ps1
cd firmware
```

| Command                         | What it does                                                         |
| ------------------------------- | -------------------------------------------------------------------- |
| `idf.py build`                  | Build for chip rev 3.x (default)                                     |
| `idf.py -p COMx flash monitor`  | Flash and open the serial monitor (Ctrl+] exits)                     |
| `idf.py -p COMx monitor`        | Serial monitor only (921600 baud, taken from the project config)     |
| `python ..\tools\screenshot.py COMx` | Save the display to `screenshots/<timestamp>.png` (close any monitor first) |
| `python ..\tools\screenshot.py COMx --open home velocity` | Open each screen and save `screenshots/<screen>.png` |
| `node ..\tools\convert-fonts.mjs` | Regenerate the LVGL fonts and `Icons.hpp` after changing `tools/convert-fonts.mjs` |
| `node ..\tools\convert-images.mjs` | Regenerate the LVGL images (`ui/images/`) from the SVGs in `firmware/assets/` (none yet) |
| `python ..\tools\console.py COMx --wait 8 "tank status" --output log.txt` | Send serial commands and capture the log (close any monitor first) |
| `idf.py menuconfig`             | Edit configuration (writes `build/sdkconfig`; copy keepers to `sdkconfig.defaults`) |
| `idf.py fullclean`              | Remove the build directory                                           |
| `& $env:IDF_TOOLS_PATH\esp-clang\esp-19.1.2_20250312\esp-clang\bin\clang-format.exe -i --style=file <files>` | Format C++ (ESP-IDF ships clang-format 19; skip `main/ui/fonts/`) |

Serial commands (type into the monitor, or send a line; `help` lists them). Robot: `tank on|off|stop|status|max <rpm>|turn <percent>|accel <rpm/s>|nodes <l> <r>|invert left|right`, `gamepad` / `gamepad pair` / `gamepad forget` / `gamepad disconnect`, `camera` / `camera stop`. From the console: `screenshot` (used by the script), `open <screen>` (names from `ui/ScreenRegistry.cpp`, e.g. `home`, `overview`, `velocity`), `specimen` (font and icon specimen screen; tap to go back), `params` (read and log every parameter in the table), `dump` (log every object the drive answers), `torque <0.1 %>` / `torque stop` (torque mode from the console), `splash` (start-up splash, held until tapped), `state` (log every `UiState` value), `stack` (least free stack of the motor and LVGL tasks), `perf` (CPU per task, heap, LVGL object and timer counts), `back`, `home`. Bench recovery (D-030): `sdo r <index> <sub>` / `sdo s <index> <sub>` (string) / `sdo w <index> <sub> <bytes> <value>` (index and sub in hex, value `0x` hex or decimal; `200C:01` and undocumented save/restore codes are refused), `nmt <hex command>`, `stop` (STOP, as the top bar's button), `reset` (fault reset), `restore`, `save`, `recover`, `calibrate` (the Faults and Tune screen actions without their confirmation; `calibrate` spins the motor), `fake blank` (report a blank parameter flash until restart, to exercise RECOVER's UI on a healthy motor; D-031), `blank` (TEST: write all ones to every parameter RECOVER covers, run time only, so RECOVER's real write path can be tested; a power cycle undoes it too). Latency (D-034): `latency level` (log the light level 10 times a second while aiming), `latency run`, `latency selftest` (sensor held against the LED), `latency stop`, `latency off` (sensor and LED off), `latency led on|off`, `latency demo` (synthetic samples for the screen, no sensor). `tools/console.py COMx "cmd" ...` (run it with ESP-IDF's Python, which has pyserial) sends commands from a script and captures the log.

Chip rev 1.3 builds go to their own directory:

```powershell
idf.py -B build-rev1 -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.rev1.defaults" build
```

When running these through a tool, capture output to a file and read it — the build prints thousands of lines. Third-party warnings (e.g. `-Wmissing-field-initializers` from `ESP_LV_ADAPTER_DEFAULT_CONFIG`) are known; our own code should build without warnings.

## Project documentation

Documentation is split into focused files under `/docs`. Read only the files relevant to the current task. See `docs/README.md` for the index.

- `README.md` (root) is for anyone building, flashing or using the robot. Keep it accurate to what exists.
- `/docs` is for development: overview, hardware, the motors, architecture, decisions, open questions.

## Project status

**2026-11-20: Sparky drives on the floor with an Xbox controller over BLE (tank mode from power-on, LB + RB arms, Y fast mode, B stops), and the OV5647 camera streams to the display (`camera`). Wi-Fi isn't up: the C6 needs reflashing.** See `docs/Milestones.md`.

- **Open questions are not settled.** Anything in `docs/Open questions.md` or marked _proposed_ must not be treated as decided. Ask rather than assume.
- **Record decisions.** When a question is answered or direction changes, add an entry to `docs/Decisions.md` (context, decision, consequences) and update the affected docs. Sparky's decisions continue from D-038.

## Motor references

`docs/NiMotion motors.md` has what this project knows about the drives, from the NiMotion manuals and bench measurements. The English CANopen manual is wrong about activation, save/restore and `200C:01`; the Chinese manual is more reliable. Read both before writing to a drive.

## Firmware architecture

Details in `docs/Architecture.md` (D-014, D-015). The rules:

- **Layers, dependencies pointing down:** `ui/` → `services/` → `protocol/` → `drivers/`, plus `debug/`. `services/` never includes LVGL or `ui/`; `protocol/` is plain C++ and reaches the bus only through `drivers/CanPort.hpp`.
- **`App` owns everything.** New parts are members of `App`, wired through constructor arguments and started in `App::begin()`. No globals or singletons.
- **State down, commands up.** Services publish snapshots (`MotorStatus`); one bridge timer copies them into `UiState` (LVGL subjects, integers in native units). Widgets bind to `UiState`; screens send commands through the service's request methods, which only queue. Events carry a sequence number; lists are service-owned models with a version subject.
- **Screens come from `ui/ScreenRegistry.cpp`:** a new screen is a `Screen` subclass and one row. Screens get a `UiContext` (state, motor, navigator). The motor keeps running across screens (D-016); motion screens start their controls from the service's targets in `UiState`.
- **Threading:** only the LVGL task (or code holding a `DisplayLock`) touches LVGL. Observer and timer user data must outlive the widgets (use `UiState`, never the screen). `lv_async_call` only for event handlers that would delete their own widgets.
- **Components:** functions with an options struct when they only render or bind; classes when they need methods after creation.

## Safety rules

The robot drives real motors from a battery that can deliver tens of amps, and its firmware will be changed by an agent. These rules apply to code and to agent actions alike.

- **Never write to `200C:01` (CommunicationSelect).** The manual contradicts itself on its encoding and a wrong value can make the drive unreachable.
- **Never use the NiMotion-internal save codes** (`0x9A899E87`, `0x9A899E80`) or the `0A`/`0B` sub-indices of the save/restore objects. Save is `1010:01 = 0x65766173` ("save"), restore defaults is `1011:01 = 0x64616F6C` ("load").
- **Every motion has a way to stop.** Today B on the controller, a tap on the screen, a lost controller, a drive fault and low pack voltage all quick-stop both tracks. The battery switch is the hard stop. Anything that moves the robot on its own (server commands, autonomy) must stop when its link or its source of commands goes away.
- **Motion needs arming.** The tracks don't move until the operator arms them (LB + RB today); a restart comes up disarmed.
- **Writes are explicit.** Parameters are never saved to a drive or factory-reset without a confirming action from the user. Run-time writes needed to operate on battery, never saved, are allowed automatically: the undervoltage limit `2001:10` lowered to 12 V (D-005), the drive heartbeat `1017` (D-023), the software position limits `607D` (D-020).
- **Undervoltage cutoff.** Below about 16.5 V (D-005) the robot stops the motors and refuses to arm.
- **Self-programming (planned).** A firmware image must pass a self-test (stop works, drives answer, link up) before it marks itself valid, so a bad build rolls back. The safety layer is not something the agent changes on its own.

## Agent behaviour

- **No automatic commits.** Only commit when the user explicitly asks, or when committing was agreed for that specific piece of work. When work is done, say it is ready to commit instead of committing. Approval to commit one change does not carry over to the next.
- **Don't reprint diffs of changes you just made.** Summarize structurally instead (which files, which functions, what behavior changed). Code blocks are still fine in discussion, planning and explanation.
- When the user's prompt contains questions or "discuss", treat it as a discussion — respond with analysis and proposals only. Do NOT start editing files or implementing changes until the discussion is resolved and the user explicitly approves a direction.
- When in doubt, ask for clarifications before taking action.
- Be critical and propose improvements to user requests when appropriate. Argue if something proposed doesn't make sense.
- Don't overengineer. This is a single-person project; prefer the simplest structure that works and refactor when a real need appears.
- **Hardware claims need a source.** Voltages, pinouts, object indices and timings come from a datasheet, schematic, manual or a measurement — say which. Mark anything inferred as unverified.
- **Public repository.** No credentials (Wi-Fi passwords, API keys), no personal data, nothing from an employer's internal documents. Secrets go in git-ignored local files.
- Propose helpful tools and skills to use to achieve user goals.
- When the user makes notes about conventions or preferences, store the decision in CLAUDE.md for future reference.
- Use the conventions used in the existing codebase as a guide for new code. Propose improvements if any of the existing conventions seem suboptimal.

## Communication style

Write the shortest thing that lets me understand what happened or decide what to do next. Length is a cost I pay on every message, and most of what gets cut is true but inert.

**Worth writing:** what changed and why it matters, anything that would surprise me, a decision I need to make and the options, a caveat that affects whether I can trust the result, and the fact that something is unverified when it is.

**Not worth writing:** detail that is interesting but leads to no action, restating what the tool output above already showed, narrating the process that got to the answer, listing what was considered and rejected unless the rejection is the point, or closing summaries that repeat the message.

**Expand identifiers from earlier work.** `D-004`, `M2`, `NAV-010` and the like mean nothing weeks later. Every time one is mentioned — decisions, milestones, plan tasks, open questions — give the identifier and a short description of what it is in the same breath: "D-004 (board powered through the RS485 connector)", "NAV-010 (docs wrap-up after the navigation shell)". This applies to proposals like "let's take NAV-005 next" too.

## Code style

C++17 or newer. `.clang-format` is the source of truth (LLVM base, 4-space indent, 200-column limit, left pointer alignment, attached braces, one argument/parameter per line when wrapped). Run clang-format after edits.

### Formatting preferences

- Blank lines around `if`, loops, `switch`, and before `return`.
- Blank line after each `case` that ends with `return`.
- Group code into logical blocks separated by blank lines, with a sentence-case comment above each block describing what it does.
- Readability over cleverness. No nested ternaries or dense one-liners.

### Naming conventions

- Classes, structs, enums: `PascalCase`. Methods, functions, variables, fields: `camelCase`. Constants and enum values: `UPPER_SNAKE_CASE`.
- Booleans: `is` / `has` / `should` / `can` prefix (`isEnabled`, `hasFault`).
- **Units in names**: `timeoutMs`, `busVoltageV`, `currentA`, `velocityRpm`, `positionCounts`. Drive-native units get named too (`currentCentiamps` for the drive's 0.01 A values) and are converted at the boundary.
- Pin fields: `pin` prefix (`pinCanTx`, `pinMotorEnable`).
- CANopen objects: named constants, never bare numbers in logic (`OD_CONTROLWORD = 0x6040`), with the sub-index alongside.
- Full words over abbreviations, except established protocol terms (SDO, PDO, NMT, EMCY, OD).

### Class structure

Order within a class:

**`public:`**

1. Nested types / enums
2. Constructor (and destructor if non-default)
3. Lifecycle methods (`setup`, `loop` or equivalent)
4. Public API, grouped by concern with `// Section` header comments
5. Static utility methods
6. Public state fields, grouped with `// Group` header comments

**`private:`**

1. Cross-module dependencies
2. Hardware-owned objects and private state
3. Setup helpers → loop helpers → event handlers → static helpers
4. Constants (`static constexpr ...`) last

Public mutable state is fine for module-owned state that other modules read. Don't add accessors purely for form.

### Comments

- Trivial fields and enum values: one inline `// comment` on the same line. Use `//`, not `///<`.
- Non-obvious fields or APIs: a full Doxygen block.
- Single-line `//` comments do not end with a period. Doxygen sentences do.
- Comments explain why, and cite the source for protocol facts (`// Manual §7.5: brake resistor threshold`).

### Doxygen

Use `/** ... */` blocks for public APIs and non-trivial internals:

```cpp
/**
 * Brief one-line summary ending with period.
 *
 * Optional extended description.
 *
 * @param paramName Parameter description
 * @return Return value description
 */
void methodName(int paramName);
```

- First line is a complete sentence; no `@brief` tag.
- Blank line between summary and `@param`/`@return`.
- Always include `@param` for every parameter and `@return` for every non-void return.
- Use `@note` for usage notes, `@warning` for critical caveats.

### UI code

Follow `docs/UI design.md` and the Firmware architecture rules above: screens are built from the shared components and theme tokens and never style widgets directly, and show state by binding to `UiState`, not by polling. Check every UI change with `tools/screenshot.py` and look at the PNG before calling it done.

### Error handling

- **Bus operations fail normally.** SDO timeouts, abort codes and a missing node are expected states, returned as results (with the abort code decoded to text), not asserted or ignored.
- **The UI shows the real error.** An SDO abort or drive alarm is shown with its code and meaning, never as a generic "failed".

## Documentation conventions

- **Language:** English.
- **File names:** Sentence case with spaces (`NiMotion motors.md`), kept flat in `/docs`. Exception: task plans go in `docs/Plans/`.
- **Links:** Standard markdown links with URL-encoded spaces (`[NiMotion motors](NiMotion%20motors.md)`), not `[[wiki links]]`.
- **Diagrams:** Mermaid instead of ASCII art. Prefer top-down (`graph TD`).
- **Mark certainty.** Distinguish verified facts (with source and date), proposals and open questions.
- **New docs go in the index** (`docs/README.md`).

## Planning

The high-level plan is `docs/Milestones.md`. Work one milestone at a time: plan it in detail with `/plan-tasks` when it starts (in `docs/Plans/`), build it, test it on the robot, then update its status and adjust later milestones with what was learned.

When planning, check against `docs/Overview.md`, `docs/Decisions.md` and the safety rules above. Every plan says how the change will be tested on the robot.

### Working style & preferences

- **Pragmatic approach**: Focus on shipping value over perfection
- **Simplicity first**: Clear, maintainable code over clever solutions
- **Progressive development**: Make it work → make it right → make it fast
- **Fast feedback loops**: Rapid iteration and validation on the real robot
