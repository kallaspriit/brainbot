# UI design

How the console looks and feels, and how the UI code is built. Agreed 2026-10-08; tokens and sizes checked on the real display through the navigation shell (2026-10-09). `ui/Theme.hpp` is the source of truth.

## Approach

- **Code-first.** The UI is written in C++ on LVGL 9, without a visual designer or generated code. A theme defines the look once; components build styled widgets; screens only put components together and never set styles themselves.
- **No mockups.** Designs are iterated directly on the device, using the serial screenshot command (below) to check each change.
- **Simple, clean, good-looking.** Few colours, generous spacing, large type, nothing decorative that doesn't carry information.

### Tools considered (2026-10-08)

| Tool | Why not now |
|---|---|
| [LVGL Pro](https://lvgl.io/pro) — LVGL's XML component editor with live preview and C generation | Another toolchain and generated code to merge; pricing not found. The most interesting option if code-first ever feels slow |
| [SquareLine Studio](https://squareline.io/) — commercial drag-and-drop designer | GUI-only, so it can't be edited as text; generated code clashes with hand-written logic; [LVGL ended its partnership with it](https://forum.lvgl.io/t/lvgl-ends-its-collaboration-with-squareline-studio/14638) |
| [EEZ Studio](https://forum.lvgl.io/t/free-and-open-source-eez-studio/12954) — free open-source designer with an LVGL 9.5 template | Same drawbacks as SquareLine, but free |
| PC simulator (LVGL on SDL) | Faster iteration, but a second build setup and UI code kept free of the BSP. Revisit if the screenshot loop is too slow |

## UX rules

- **Big touch targets:** at least 64 px. The console is used at a bench, sometimes with dirty fingers or gloves.
- **Dark theme, high contrast**, for workshop lighting.
- **Colour has one meaning:** red only for STOP and faults, amber for warnings, green for running/OK, blue accent for selection and primary actions.
- **Numbers don't jump:** live values use a fixed-width digit font, always with their unit.
- **Every touch gets feedback:** a pressed state on everything tappable; no silent failure — errors show the real reason (code and meaning).
- **STOP never moves:** top bar, right edge, on every screen.
- **Writes are confirmed:** anything that writes to the drive goes through a confirmation (safety rules in `CLAUDE.md`).
- **Shallow navigation:** home cards, one level deep, back always in the same place.
- **Surfaces for tiles and controls, flat for reports:** a value tile, a control or a chart sits on a surface; lists and reports (Faults, Tune & parameters) use headings, spacing and a divider instead of boxes, which saves the padding for content.
- **No layout shift:** something that comes and goes (the unsaved bar) keeps its place and greys out instead of appearing.

## Design tokens

Tuned on the device, 2026-10-09.

### Colours

| Token | Use | Value |
|---|---|---|
| `background` | Screen | `#0F1216` |
| `surface` | Cards, panels | `#171B21` |
| `surfaceRaised` | Pressed, selected, dialogs | `#1F252D` |
| `border` | Dividers, outlines | `#2A313B` |
| `text` | Primary text | `#E8ECF1` |
| `textMuted` | Labels, secondary text | `#9AA4B2` |
| `accent` | Selection, primary actions | `#3B82F6` |
| `success` | Running, OK | `#22C55E` |
| `warning` | Warnings | `#F59E0B` |
| `danger` | STOP and faults only | `#EF4444` |
| `chartSecondary` | Lines on a chart's second scale (motor current) | `#C084FC` |

### Spacing and shape

- Spacing scale: 4, 8, 16, 24, 32 px.
- Corner radius: 12 px.
- Minimum touch target: 64 px.
- Top bar: 72 px high. Buttons: 64 px, 88 px for a screen's main action (START). Top bar buttons (back, STOP): 56 px.

### Type

| LVGL font | Source | Use |
|---|---|---|
| `inter_regular_16/20/24` | Inter 4.1 Regular | Body text, labels |
| `inter_semibold_20/24/32` | Inter 4.1 SemiBold | Titles, buttons |
| `mono_medium_16/24/32/48` | JetBrains Mono 2.304 Medium | Live numbers (fixed-width digits); 16 for codes, raw values and times in lists |
| `icons_24/32/48` | Material Symbols Rounded, outlined, weight 400 | Icons, only the ones listed in `tools/convert-fonts.mjs` |

Text fonts cover printable ASCII plus ° ± µ · × – — • … → ≈ Ω. Icons are used through `Icons::` constants (UTF-8 strings) in `ui/fonts/Icons.hpp`. All fonts are 4 bpp and add about 170 KB of flash. Built in NAV-003 (2026-10-08); checked on the device with the `specimen` serial command.

Inter's tabular digits are an OpenType feature that `lv_font_conv` can't enable, hence a separate monospace font for numbers. To add a size, a glyph or an icon, edit `tools/convert-fonts.mjs` and run `node tools/convert-fonts.mjs`; the generated files in `firmware/main/ui/fonts/` are committed. Source TTFs and licences (SIL OFL 1.1 for Inter and JetBrains Mono, Apache 2.0 for Material Symbols) are in `firmware/fonts/source/`; the 15 MB Material Symbols font is downloaded on demand into `tools/.cache/`, pinned to a commit and checked by SHA-256.

## Components

The set the planned screens need. Built as needed, not all up front. Built so far (2026-10-12), in `firmware/main/ui/components/`: `Button`, `HomeCard`, `Badge`, `StopButton`, `ValueTile`, `InfoPanel` (a flat section) and `createDivider`, `NumberControl` and `NumberPad`, `SegmentedToggle`, `TrendChart` with its `VelocityChart` and `PositionChart` configurations, `Dialog`, `ConfirmDialog`, `FaultReactionsDialog`, `OptionPicker`, `MessageLine`, `BatteryIndicator`, `Splash`, `TabStrip`, `TorqueChart`, `ParameterList`, `SetupList` (also the Tuning list), `TuningTab`, `UnsavedBar`, and `TopBar` with the drive status and STOP (NAV-007).

### Screen structure

Screens are defined in `ui/ScreenRegistry.cpp` (one row each) and switched by `ScreenManager`, which owns the top bar and an overlay layer for dialogs, both on LVGL's top layer. A screen is created when opened and deleted when left, with `onEnter`/`onLeave` hooks and a back stack; it builds its widgets into the content area below the 72 px top bar and shows state by binding to `UiState`. Full description in [Architecture](Architecture.md#ui-structure).

| Component | What it is |
|---|---|
| Button | Primary, secondary and danger variants, plus outline versions of primary, secondary and danger for an action that is tappable but would do nothing visible (GO at the target, STOP with nothing to stop) or a lesser one (REACTIONS, FACTORY RESET), and a ghost variant (a dialog's X). Switchable after creation; standard heights, and a compact one (44 px drawn, 64 px touch area) for the top bar STOP and buttons inside a section |
| Home card | Icon, title, one-line status, disabled state |
| Top bar | On home "SPARKY"; elsewhere back and the title. Motor identity, state badge (the fault's name when faulted), alarm badge (a warning, or an alarm that halts the motor such as a stall: amber), both opening Faults, battery and bus voltage (hidden at 0), CAN status, compact STOP (filled while the drive can move, outline otherwise). Without a drive: "Searching for a motor" with an info icon, tapping goes home |
| Value tile | Label, large fixed-width number with unit, optional warning colour |
| Number entry | `NumberControl`: a header with label and value (tap it for the pad), step buttons and an optional slider. Fixed-point `decimals` (position in turns uses 3). A screen can put widgets in the header (`getNumberControlHeader`), as the Position screen's Absolute/Relative toggle does |
| Number pad | Full-screen keypad on the overlay layer with the range shown; a decimal point key when the value has decimals; out-of-range entries show the range in red and can't be confirmed. The title wraps. Parameters open in the unit they are shown in (position units as turns, rpm, rpm/s); only hex codes and named values open raw |
| Segmented toggle | Two segments in one outlined frame with a shared divider, bound to an integer subject; the picked one has an accent tint and accent text, so it reads as a choice, not as two buttons (Absolute / Relative, Velocity / Position). `isStretched` fills a given width; otherwise as wide as the labels |
| Trend chart | Scrolling line chart, about 10 s, fed from a service sample buffer on a 20 ms timer. The range follows the data (optionally without reaching for zero), labelled in the corners; lines can sit on a second scale on the right, labelled in their colour (motor current). Legend under the chart; the screen says what it shows |
| Dialog | `openDialog` on the overlay layer: title, one-line subtitle, an X, a body that scrolls (with a visible scrollbar) past 420 px; actions added with `addDialogActions`. Used for the fault reactions list |
| Confirm dialog | A dialog with the points to know, what will be written, CANCEL and a red confirm button. Used by FACTORY RESET (restore defaults), SAVE (the list of what ends up in flash) and node ID and bitrate changes |
| Option picker | A dialog with one full-width button per choice, the current one marked; a tap picks and closes. For named values (input functions, homing methods, fault reactions, bitrates) |
| Tab strip | Text tabs under the top bar with an accent underline on the picked one; the screen swaps the content below |
| Unsaved bar | Under the tabs: the count of unsaved changes, FACTORY RESET (red outline, the restore-defaults confirm; on the Parameters tab only, as Setup has its own), REVERT and SAVE. Always there; with nothing unsaved it says so and both buttons are disabled |
| Message line | The motor service's last message with an icon and colour for its level: info in plain text with a muted info icon, warning amber, error red. Fades in on every new message. Replaces toasts for now: a message stays until the next one, so a failure reason or an instruction isn't missed |
| Parameter list | Expandable groups of parameter rows (name, value in its unit, marks for unsaved, changed from factory, console session value, locked), a filter box with an on-screen keyboard that hides non-matching groups and rows (1 s after the last key), and a details dialog per row with EDIT |
| Setup / Tuning list | A curated list from a section table: rows with a name, a note (unsaved and the earlier value, factory value, console value, or why it can't change now) and a value button that opens the number pad or an option picker. Gains have − / + steppers of about 10 % that write live and stop at the value from before the first change. A section can hold console settings too (the step test) |
| Splash | At start-up, on LVGL's system layer above everything, on white: the panel shows white while it starts and that can't be avoided in software (bench 2026-10-11), so the splash continues it. "SPARKY" fades in over 0.8 s, hold 1.5 s, fade out in 0.2 s, then the white fades away in 0.4 s to the home screen built underneath. A tap skips. The motor search runs meanwhile. The version (`firmware/version.txt`) and the build's commit sit small at the bottom |
| Battery indicator | A drawn battery filled by the bus voltage between the cutoff (16.5 V) and a full pack (20.5 V), a bolt at 21.5 V or more (a supply, not a pack) |

**The Faults screen** is the first flat report (2026-10-11): two columns with a divider. Left: the active alarm (headline, error code, its reaction, statusword bits, error register; failing run checks in amber; a compact REACTIONS button at the top right), the session log (scrolls, coloured by severity), the message line, and RESET FAULT and FACTORY RESET. Right: the alarm history, newest first, coloured by each entry's reaction, raw words in the mono font. Alarm colours follow the reaction: red for a fault, amber for a halt or warning, muted for ignored.

**The Torque screen** (2026-10-12) mirrors Velocity: the torque from the measured current in mN·m (large tile), speed and current, a chart of target and torque in mN·m with the speed on the second scale, and on the right the target torque and slope in mN·m, the rated torque and speed limits, START / SMOOTH STOP. mN·m is the unit for small-motor torque; the drive's per mille of the rated current is converted with the rated torque `6076`.

**The home screen** (since 2026-10-22, D-033) is a 3×3 grid shown with or without a drive: Overview, Faults, Parameters / Velocity, Position, Torque / Tuning, Calibrate, Latency (reordered 2026-11-03: the drive's state, then motion, then tuning and tools). Cards that need a motor fade while none answers; the search panel ("Looking for a motor") is at the top of Overview.

**The Latency screen** (2026-10-22, D-034) needs no motor. Left: the light on the sensor as a bar (bright to the right, a faint band for the range over the last 100 ms, marks for the learned dark in grey, threshold in amber and bright in white, the contrast under it), and below it one panel that shows a four-line placement guide before any run, the samples as dots during a run (in order, with the running median as a line; smaller dots above 100 samples), and after a run a histogram with round values under the bars and the median as a vertical line, or the dots again when the spread fills fewer than 5 bins (a self-test, a bench check). The dot chart's scale starts at 0–1 ms. Right: the median in large type, p95, a Run panel (samples, min to max, p5–p95 spread; times below 10 ms with two decimals), the status line (red on failure, green or red for a self-test, amber for ripple), an LED off / on toggle for aiming, SELF-TEST and START, and during a run a full-width STOP in their place. The top bar shows only back and the title here while no drive answers (D-036); with a drive, its STOP is back and still only stops the motor. The dot chart's range starts at a round value below the lowest sample, labelled in a gutter on the left; the histogram has about 5 samples per bin (5 to 40 bins).

**Tuning, Parameters and Calibrate** were the three tabs of one Tune & parameters screen (2026-10-12) until D-033 split them; each has the unsaved bar, FACTORY RESET on the Parameters tab only. The Parameters screen keeps two tabs, Setup and Parameters. What follows describes the Tuning screen, which keeps the old tab's layout. Tuning splits in two: the chart of the running motion (velocity, or position or torque while that mode runs) with the step test's mode toggle and START / STOP TEST under it, and on the right the Tuning list (step test settings, speed and position loop, filters, gain switching). Setup lists the settings that matter on a bench or in a PTU (current limits, ramps, fault reactions, inputs, homing, supply, node ID, bitrate and restore defaults). Parameters is the full table.

Values reach widgets through `UiState` (LVGL 9 subjects): a bridge timer copies the motor snapshot into it and bound widgets update themselves; `ui/Bindings` formats fixed-point values. The motor task never touches LVGL.

## Screenshot command

For iterating without photos: `python tools/screenshot.py COM6` sends `screenshot` over the USB serial console; the firmware renders the screen and the top layer with `lv_snapshot`, blends them, and streams them run-length encoded; the script writes a PNG. About 1.7 s per capture (built in NAV-002, 2026-10-08). See [Development setup](Development%20setup.md#screenshots).
