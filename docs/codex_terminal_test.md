# Terminal test implementation — terminal-v1

Implemented from [CODEX_TERMINAL_TEST_PLAN.md](CODEX_TERMINAL_TEST_PLAN.md).
The pulled repository contained that uppercase plan name. This file documents
the implemented commands and limitations; the original plan remains unchanged.

## GPIO32 QUICK addition (uploaded on COM4)

The current build enables only the normally-open QUICK button on GPIO32 to GND.
It uses INPUT_PULLUP. For the 0.5 m cable an external 10 kohm pull-up between
GPIO32 and 3.3 V is recommended; the button still closes to GND. Route signal
and ground together. There is no direct VCC connection through the button.

Sampling runs in a separate task at nominal 5 ms, with 30 ms stable-state
debounce and an eight-event queue. It remains active during TFT/SD work.
One press produces one QUICK; holding does not repeat. A stable release is
required before another press. A button held at startup must first be released.
Hardware and terminal both use `TerminalApp::quick`, including menu confirmation,
quick views and alarm acknowledgement. The terminal prints `BUTTON QUICK: GPIO32`
for each accepted physical press. LEFT/RIGHT/SETTINGS remain terminal-only.

The change was initially prepared without upload, then uploaded on COM4 after
explicit authorization. All 20 timing and 57 control assertions passed on ESP32,
including the six debounce assertions. The physical switch is not yet verified.
Now verify one event per press, no hold repeat,
quick-page timeout, and alarm acknowledgement. Release the physical button
during the automated terminal harness.

## Start the terminal

From the project directory in PowerShell:

```powershell
.\.venv\Scripts\platformio.exe run
.\.venv\Scripts\platformio.exe run --target upload --upload-port COM4
.\.venv\Scripts\platformio.exe device monitor --port COM4 --baud 115200 --filter send_on_enter --echo
```

Close an existing monitor before upload or the automated test. Ctrl+C closes
the monitor. Every command below requires Enter; CR, LF and CRLF are supported.
The reader is bounded to 191 characters, discards an entire overflowing line,
and consumes at most 128 input bytes per acquisition-loop iteration. Commands
are case-sensitive, lowercase, except the preserved single-letter aliases.

```text
help
status
selftest
sd readback
sd selftest
```

`selftest` runs the preserved 20 GPS geometry/timing assertions and the isolated
control tests. The control tests write only the `lt-selftest` NVS namespace.
`sd selftest` exercises load, sequence and corrupt-slot fallback in `/test/`,
using reserved geometry 54455354 and date 20991231. It never replaces a real BEST.
`sd readback` is an explicit temporary-file diagnostic, not the normal writer.

## Configurable QUICK pages and themes

SETUP now starts with DEFAULT SCREEN, GREEN 1, GREEN 2, GREEN 3, BACKGROUND,
then the existing speed/pressure/temperature/acceleration unit entries.
Each QUICK slot independently selects one of eight pages; duplicates are allowed:

| Command value | Page |
| --- | --- |
| last_lap | LAP number + last completed lap time |
| rear_pressure | REAR PRESSURE + measured value |
| front_pressure | FRONT PRESSURE + measured value |
| best_lap | BEST LAP + complete reference lap time |
| max_angle | Largest absolute valid lean angle in this session |
| max_speed | Highest valid GNSS speed in this session |
| max_acceleration | Highest positive valid longitudinal acceleration |
| max_deceleration | Largest braking magnitude, displayed positive |

These pages have only the heading and a very large value. Units are in the
heading (angle uses the degree sign); missing values are `--`. Temperature,
battery, receipt age, freshness text and comparison footers are removed from
the pressure/last-lap pages. TEST is still explicitly marked during simulation.
The ordinary LAP HOLD and green BEST overlays use the same minimal layout.

```text
set quick1 last_lap
set quick2 max_angle
set quick3 max_speed
set background black
set background white
set background blue
```

BLACK uses light text, WHITE uses dark text and DARK BLUE uses light text.
Warning/alarm and new-record colors retain their meaning. Units are configured
with `set speed kmh/mph`, `set pressure bar/psi/kpa`, `set temperature c/f`,
and `set accel g/ms2`, or through their SETUP entries. Alarm limits and stored
maxima remain in canonical bar, km/h and g; display conversions do not change them.

The three presses are consecutive QUICK page advances, not a double-click
gesture. Five seconds without a press returns the base screen; the next press
starts at GREEN 1. Confirmed mappings, theme and units persist in NVS. Schema 4
migrates the exact old schema-3 record, retaining confirmed settings and adding
the default LAST LAP / REAR / FRONT mapping and BLACK background. A corrupt
record is rejected, not silently interpreted as another schema.

Peaks belong to the current boot/session and reset on day/track/timezone scope
changes. `max reset` clears only the current REAL/TEST source's peaks. Lean-zero
or mounting calibration changes clear IMU-derived peaks. Invalid IMU samples
do not produce an angle/acceleration maximum; without calibration those pages
remain `--`. Dynamic IMU maxima have the same experimental accuracy limits as
the camera estimator. Pressure/IMU loss does not erase earlier valid session peaks.

```text
test on
test motion -44 180 0.55
test motion 40 170 -0.80
set quick1 max_angle
key quick
status
test off
```

`test motion` affects isolated TEST maxima only. `v` on a minimal page checks
that the former footer area is actually background in TFT RAM.

## Physical panel: green, red, left/right

Current option-editor bench verification on COM4: firmware write/hash PASS;
`selftest` reports 20 timing + 139 control assertions passed, zero failed.
The pressure-only hardware measurement increased the uncommitted value by
2.00 bar in 4.078 seconds and verified unchanged confirmed configuration.
These uploads/tests were performed with the SD card removed.
Physical contact operation still needs a manual check; the bench read RIGHT
closed, so return the real direction switch to CENTER before manual navigation.

```powershell
.\.venv\Scripts\python.exe tools/test_panel.py --port COM4
```

Run stationary, in REAL mode with settings closed, and leave physical contacts
untouched during the test. It preserves confirmed configuration and exits with
virtual GREEN released, RED OFF, direction CENTER. Its log is
`.platformio/panel_integration.log`; built-in assertions were captured in
`.platformio/panel_selftest.log`.

Separate-option editors, directional GREEN stepping and faster pressure repeat
are uploaded. A full integration attempt was interrupted by the real motion
guard when GNSS speed exceeded 6 km/h; this was recorded, not bypassed. A later
pressure-only run met the five-second target. Minor rendering jitter is handled
with scheduled repeat deadlines instead of adding the interval to each late poll;
long stalls discard the backlog rather than issuing multiple steps in one tick.

The subsequent full run passed 19 checks, including all choice-editor checks and
+2.20 bar in 4.62 s with unchanged confirmed settings, before REAL GPS motion
blocked root re-entry. The last three BACK/rearming checks then passed separately
in `.platformio/panel_root.log` (a fresh OFF/ON retry was needed once). The full
uninterrupted suite is not claimed PASS. Pressure-only measurement is saved in
`.platformio/pressure_timing.log`. No GPS guard was disabled to obtain these results.

The category tree and raw CAMERA fallback are uploaded and tested. On the real
uncalibrated IMU `status` confirms CAMERA angle_valid=1 raw=1. Tests preserve
confirmed configuration. Visual menu layout/colors and physical presses still
need the user's manual check.

Current pin assignment supersedes the old GPIO16 proposal:
GREEN momentary GPIO32, RED latch GPIO5, LEFT GPIO34, RIGHT GPIO35.
All contacts close to common GND. GPIO34 and GPIO35 each need an external
10 kohm pull-up to **3.3 V**, not VIN/5 V. They have no internal pull-ups.
GPIO32/5 use INPUT_PULLUP; external 10 kohm is recommended for long wires.
GPIO5 straps SDIO-slave timing at reset; this project does not use SDIO slave.
SD SPI retains GPIO13/15/2/14. GPIO0/12 are not used for buttons.

See [README: wiring and navigation](../README.md#gumbi-vezava-in-navigacija)
for the physical operation. The new menu labels are GREEN 1/2/3; existing
`quick1/2/3` setting tokens and `key quick` remain compatible.

The contact acquisition task samples all four inputs every nominal 5 ms, with
30 ms debounce on both edges. Short GREEN emits once on release. A 2-second
hold emits once and consumes the release. RED only arms entry after OFF/ON;
GREEN must start afterwards. Startup-held inputs require release/rearming.
LEFT/RIGHT now select the modifier for GREEN; the switch alone never moves a
row. Leave the direction held and click GREEN to step repeatedly. CENTER GREEN
selects/opens/confirms. Both-closed ignores menu actions. Outside settings,
GREEN still performs normal review/acknowledgement. Contact-queue overflow closes settings and resets
arming rather than replaying an incomplete gesture.

Entry requires direction CENTER, RED ON then GREEN held 2 seconds, no running REAL timing lap,
no fresh REAL GPS speed above 5 km/h and no unacknowledged alarm. Speed freshness
is <1500 ms with a valid current fix. TEST does not substitute fake motion for
these guards. Motion during a hold cancels that attempt. Motion/lap start while
the menu is open closes it, discards an unconfirmed edit and requires OFF/ON.
No GPS fix still permits deliberate RED + hold entry; it is not proof of rest.
The category menu is DISPLAY, CALIBRATION, UNITS, TRACK, TPMS, STORAGE, BACK.
LEFT/RIGHT + a GREEN press moves up/down a scrolling list. CENTER + short GREEN
enters a category, opens an editor, confirms a value or selects BACK. A setting
opens a separate full-page choice list, including BACK to cancel. Numeric
editors show the active value large. CENTER long GREEN remains an alternative
to short select/confirm. BACK in a child returns to categories; root BACK exits
to RACE and requires RED OFF/ON before re-entry. RED OFF exits immediately and
discards an unconfirmed edit. Long GREEN no longer exits the tree menu.
The selected row is RED, the confirmed setting GREEN until movement/new editing.
All menu text is uppercase; row/value changes use buffered updates without full
clears. Opening/closing a separate editor redraws the page once.
The existing direct `switch settings` diagnostic retains flat edit behavior;
physical contacts and `button ...` use the new category tree.

Guarded terminal simulation (real elapsed time, no `test tick` required):

```text
button red off
button direction center
button red on
button green down
```

Wait 2 seconds, then `button green up`: SETTINGS opens without entering edit.
`button direction right`, then GREEN down/up, moves one item. More GREEN clicks
advance more rows without recentering. CENTER + GREEN enters a category/editor
or commits a value and returns. Use each level's BACK for navigation, or
finish `button red off`. Root BACK must not reopen until RED OFF/ON.
`status` includes PANEL contact state, arming, queue drops and REAL motion guards.
Do not touch physical contacts during terminal simulation.

Numeric hold: LEFT/RIGHT + GREEN down steps once immediately. After 600 ms,
pressure MIN/MAX repeat every 80 ms (0.05 bar/step); other numeric fields repeat
every 250 ms. Zero to 2 bar takes 3.64 s in the deterministic timing test. The
integration test holds for four seconds and verifies at least 2 bar of increase
within five seconds, without committing the draft. Bounds clamp at zero/20 bar.
No catch-up burst occurs after a late UI/SD iteration. Release, direction change,
alarm or motion stops repeat. A direction-to-center change during a held press
cannot become a confirm. Enums/categories never auto-repeat or long-confirm
when GREEN began with a direction held. MIN then MAX commit atomically.
`status` includes editor_row/choices/numeric/range_part and EDITOR value.

CAMERA now uses a fresh (<150 ms) accelerometer roll angle if the IMU is not
calibrated, respecting the existing lean zero and labeling RAW / UNCALIBRATED.
Loss, stale samples and undefined roll are still invalid. This display fallback
does not make the dynamic estimator or IMU maxima valid, and is not substituted
for a calibrated estimator that fails its timing/attitude checks. `status`
includes CAMERA angle_valid/raw/angle_deg and MENU TREE depth/category/row/confirmed.

## Menu and switch simulation

```text
screen race
switch direction right
switch direction right
switch direction center
switch direction left
switch direction center
switch settings on
key right
key quick
key right
key quick
switch settings off
```

These legacy direct diagnostic events bypass panel arming. In this legacy
path LEFT selects RACE, RIGHT selects CAMERA; CENTER leaves the screen alone.
Physical contacts and `button direction ...` do nothing outside settings.
Repeated switch states do not generate new edges. A move directly from LEFT to
RIGHT without CENTER does not generate a second step. `key left/right` is the
convenient complete center->direction->center gesture.
SETTINGS ON is latched. LEFT/RIGHT select a menu item; QUICK enters editing,
LEFT/RIGHT alter the draft, QUICK confirms and saves. OFF discards only an
unconfirmed draft. FRONT/REAR MIN/MAX edit MIN first, then MAX; their second
confirmation commits the pair. NAZAJ leaves settings until OFF->ON.
Calibration actions and SD status are also reachable from this menu. Sensor IDs
can be typed with `set`; the menu offers explicitly named SIM-F/SIM-R placeholders.
No BLE discovery is implemented. All four panel inputs now have the hardware
assignments listed above; new contacts still require physical testing.

`screen race/camera` changes the runtime base. `set base race/camera` changes
and saves the startup default. CAMERA uses the RACE dashboard layout: rear
pressure top left, front pressure top right, a large live lap timer, centered
speed below the timer, and the incremental lean bar with the degree value at
the bottom. GPS/battery/SD and lap/delta indicators remain. Speed follows the
selected KM/H or MPH unit. The uncalibrated angle feeds that same lean bar;
RAW is a small status-line label, not a separate angle-only screen. Missing
speed/angle shows `--`. This mode is a display for filming, not video recording.

QUICK outside settings cycles LAST LAP -> REAR PRESSURE -> FRONT PRESSURE.
Each press renews a five-second deadline. On expiry the current base returns.
Before the first completed lap LAST LAP is `--`. Missing or stale pressure
shows SENSOR LOST / `--`, never a fabricated zero. The pressure quick page
also shows temperature, battery and receipt age when available.

## LAP HOLD, BEST and sectors

```text
test on
test lap 90000
test lap 88000
test tick 6000
test sector 1 28000
test sector 2 57000
test lap 87000
status
```

The first lap establishes a reference and a normal hold. The faster second
lap shows a full green BEST page. `test tick` advances only the TEST UI/sensor
clock; it does not alter physical GNSS, IMU sampling, calibration or real timers.
`test lap` is a synthetic completion event, not a GPS replay or gate detector.
It accepts 30000..600000 ms. If S1/S2 were supplied, their ordered cumulative
times must fit inside the lap; otherwise the simulator makes three equal
synthetic sectors. Physical laps still require both actual gates in order.

At each line the next lap starts immediately. Only the displayed result is
held for 5..15 seconds. Expiry is nonblocking and counted from the event, also
while an alarm/menu/quick page covers it. The initial reference is not a BEST
improvement. Comparison uses the complete old reference before replacing it.
At S1/S2 delta is cumulative; finish delta is the complete lap. Delta resets
for the next lap. ±10 ms is neutral. Three display segments are independent
of the three timing sectors; each represents a started 0.1/0.2/0.5/1.0 s step.

The historical AiM PomposaMotoL start/finish remains at 44.710284,12.214867.
The two project-derived thirds are 44.712024124,12.214997752 and
44.708885544,12.214549052. They are not official AiM split points. Equivalence
to the current Solo 2 Pomposa Long export remains unverified. Provenance and
geometry are in [pomposa_long.json](../tracks/pomposa_long.json).

## Pressure alarm example — synthetic values, not recommended tire pressures

These settings are persisted. Note your own settings with `status` first.
The automatic test restores them; the following manual example intentionally
changes them. Configure actual tires from their own requirements later.

```text
test on
set alarm off
set front_id SIM-F
set rear_id SIM-R
set front_range 2.0 3.0
set rear_range 2.0 3.0
set hysteresis 0.05
set confirm_ms 1000
set alarm on
test pressure front 1.70
test pressure rear 3.50
test tick 1100
test pressure front 1.70
test pressure rear 3.50
status
key quick
```

The red page shows actual injected **F 1.70 bar LOW** and **R 3.50 bar HIGH**,
not threshold values. QUICK acknowledges both and returns RACE without also
opening a quick page. Persistent deviations remain a small `!` indicator.
An opposite HIGH/LOW event or another wheel is independent. If SETTINGS was
ON at acknowledgement, OFF->ON is required to reopen it.

Rearm and stale-data test:

```text
test pressure front 2.50
test pressure rear 2.50
test tick 1100
test pressure front 2.50
test pressure rear 2.50
test pressure front 1.70
test tick 1100
test pressure front 1.70
key quick
test sensor front lost
test tick 120001
status
set alarm off
test off
```

Only new accepted packets advance confirmation/recovery. Polling an old sample
cannot confirm an alarm. Normal pressure must remain within MIN+hysteresis and
MAX-hysteresis across fresh samples for the confirmation interval. Freshness
defaults to 120000 ms and is configurable for the eventual sensor's real
transmission interval. Missing data is not low pressure.

Production TPMS adapter: call `TerminalApp::ingestPressure(false,id,bar,tempC,battery,
receivedMs,packetToken)` for a newly decoded frame. The assigned identity must
match F/R; duplicate nonzero tokens and older receipt times are rejected.
Reassigning an identity clears its prior values. A BLE decoder/device has not
been selected or tested. TEST uses a separate source and never a real BEST.
Every simulated page has TEST on screen; terminal replies identify TEST/REAL.
`test off` clears simulated references, alarms and quick pages and returns real
sources. Real acquisition and timing continue in the background during TEST.

## Settings commands

```text
settings show
set base camera
set speed mph
set pressure psi
set temperature f
set accel ms2
set track 1
set hold 8
set delta 0.5
set utc_offset 60
settings reload
```

Other values: base race, speed kmh, pressure bar/kpa, temperature c, accel g.
Hold is an integer 5..15, delta is 0.1/0.2/0.5/1.0 seconds. `track` is a
one-based catalog index; only Pomposa exists currently. UTC offset is minutes
-720..840, fixed (no automatic daylight-saving change). All ranges and
hysteresis are internally bar; `set front_range MIN MAX` / `rear_range`
always take bar regardless of display units. `set confirm_ms 0..10000`,
`set stale_ms 1000..600000`, `set front_id ID`, `set rear_id ID`, and `none`
to clear an ID are supported. MIN must be below MAX, F/R IDs must differ,
and an enabled alarm requires both ranges and assignments. Default alarm is OFF.
NVS stores validated, versioned/checksummed confirmed settings only. IDs are
at most 23 ASCII letters/digits or `:`, `-`, `_`, `.` (e.g. a MAC address).

## Daily reference and SD

A dated reference needs a valid, fresh GNSS date/time and fix, with two
consistent local-date epochs. Invalid calendar dates are rejected. Without
one it is explicitly an undated SESSION reference; it is not saved as a daily
BEST. Changing date, timezone or geometry discards the active lap and isolates
the reference before another start. Geometry identity includes all gate
coordinates, direction, width and timing policies. Reference load waits for
the SD mutex without allowing an unreferenced start; SD absent/failure allows
session-only timing. Manual `reference reload` requires REAL and no active lap.

```text
test off
set logging on
sd status
screen camera
screen race
sd inspect
set logging off
```

Real sessions are `/sessions/sNNNNNN.csv`; TEST sessions are `/test/sNNNNNN.csv`.
Names never overwrite existing files. Changing source/settings/day starts a
new file when logging is enabled. Tagged CSV contains firmware/schema/source,
settings, calibration matrix/bias, all gate geometry, GNSS epoch and local
receipt time, position, speed, course, quality/loss counts, raw IMU at actual
sample times, estimated angle/acceleration/quality, timing and alarm events.
TPMS records originate only on new packets; no stale GNSS copies are logged
as new samples. Simulated pressures/laps are labelled TEST in their own file.

IMU capture runs in a separate 20 ms FreeRTOS task; its queue holds 64 samples.
The main consumer uses actual microsecond dt. SD has its own task with a
64-record queue, persistent open file and nominal 500 ms flush interval when
healthy. It does not close/reopen/read back each row. `status` reports dropped
records and write errors. Full metadata is emitted per session. `sd inspect`
explicitly reads at most the last 8192 bytes and reports actual IMU intervals.
An SD failure stops its file, not timing/UI/alarms. The final queued/buffered
tail and filesystem metadata can be lost at sudden power-off; flush is not
a power-loss guarantee. Logging is OFF by default and persists once confirmed.

Dated BEST uses alternating checksummed slots with version, day, geometry,
lap and its three sectors. Replacement verifies a temporary file and preserves
the other valid slot; corrupt-slot fallback is tested on the physical card.
These are separate from session logging. TEST cannot write a production slot.

`test sd slow` delays TEST writes by 200 ms, `test sd fail` fails TEST writes,
`test sd normal` restores them. These commands require TEST ON and do not
inject faults into real reference storage or real files. Reopen logging after
a simulated failure (`set logging off`, `set logging on`).

## IMU calibration and experimental dynamics

Bike axes: +X forward, +Y left, +Z up; acceleration in g, gyro in deg/s.
Rotation rows are these bike axes expressed in sensor coordinates, determinant
+1. Gyro bias, the full rotation and user lean zero are separate quantities.

1. Bike upright and stationary: `imu upright`, remain still for two seconds.
2. Lean a known modest amount LEFT (10..70 degrees), stationary: `imu left`.
3. After capture, confirm which sensor axis points forward, e.g. `imu forward x+`.
4. Upright again and stable: `imu zero` sets user lean zero separately.

Capture rejects excessive variance, insufficient samples, non-1g norm and
motion. The left capture determines lateral sign; forward must agree with
the orthonormal right-handed basis. A manual mount `imu mount x+ y+ z+` is
also available after the upright bias capture; it validates all three axes.
No 90-degree physical tilt is required. Committing a new rotation resets old
user zero, so an offset from the previous sensor frame is not reused.

The experimental estimator integrates all body gyro rates with actual dt and
uses transformed acceleration with gravity removed for longitudinal output.
GNSS aiding uses changes of the east/north velocity vector (speed AND course),
GPS epoch spacing and an approximate midpoint mapped to receipt time. It is
not PPS-synchronized; UART latency and temporal alignment remain limitations.
Accelerometer correction is suppressed in moving gyro-only operation rather
than treating sustained cornering force as gravity. Fresh approximate GNSS
aiding or a stationary condition allows bounded correction. No magnetometer
is assumed. Body gz is not used as a vertical yaw rate. Labels distinguish
UNCALIBRATED, GYRO ONLY, STATIC, GNSS APPROX, timing gaps and lost IMU.

This is not field-validated dynamic motorcycle lean: road inclination, GNSS
latency/noise/course at low speed, vertical acceleration, vibration, tire slip,
gyro drift and GNSS loss limit it. The supplied forward axis must be physically
verified. Raw samples remain in SD logs for further work. A completed physical
wizard and field validation have NOT been performed during these terminal tests.

## Repeatable test and legacy commands

Verified on ESP32 COM4: build/upload PASS, 20/20 timing assertions, 51/51
control assertions and 28 terminal integration scenarios. Physical SD tests
passed readback, reference reload/corrupt-slot fallback, TEST separation,
injected slow/failing writes, overflow accounting and REAL GPS/IMU logging.
The sampled tail across RACE/CAMERA changes had IMU dt 19454..20499 us.
These values describe this bench run, not guaranteed timing bounds.

```powershell
.\.venv\Scripts\python.exe tools/test_terminal.py --port COM4
```

The harness backs up settings to `.platformio/terminal_settings_before.json`,
uses TEST data, exercises SD diagnostics/faults and a brief REAL raw-data log,
then restores changed settings and REAL mode in `finally`. It does not change
calibration or inject physical position. Raw transcript: `tools/terminal_test.log`
(ignored by git). A restore failure is explicitly reported, with backup path.

Preserved one-character commands require Enter: `t` geometry tests, `v` TFT RAM
readback, `n` legacy diagnostics, `s` settings, `q`/`b`/Escape exit, `k` track
menu, `g` green preview, `z` calibrated lean zero, `c` zero reset, `r`/`p`
stationary statistics. `n` is rejected in TEST so an unlabelled diagnostic
screen cannot conceal the TEST source. `legacy on` enables immediate single
characters; `!` returns line mode. Never type word commands in immediate mode.

Physical switch wiring, BLE radio decoding, track crossing accuracy, display
appearance/flicker from the side, true power-loss endurance and dynamic riding
accuracy still require physical checks. The GPIO32 addition above is uploaded; GPIO5/34/35 are now enabled in the panel firmware; physical testing remains.
