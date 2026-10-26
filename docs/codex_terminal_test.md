# Terminal, settings and development guide

Current behavior is summarized in [README](../README.md). This guide describes
commands and repeatable development checks, not a log of previous bench runs.

## Build and terminal

The default `esp32dev` build has all acquisition, timing, UI, calibration,
pressure alarm and SD features. `esp32dev-dev` additionally enables simulator,
button injection and selftests via `LAP_TIMER_DEV=1`; none is enabled by default.
GPIO and the physical panel gestures are identical in both builds.

```powershell
.\.venv\Scripts\platformio.exe run -e esp32dev
.\.venv\Scripts\platformio.exe run -e esp32dev-dev
.\.venv\Scripts\platformio.exe device monitor --port COM4 --baud 115200 --filter send_on_enter --echo
```

Every command ends with Enter; Ctrl+C closes the monitor. No immediate or
single-letter legacy mode remains. `help` lists commands available in the
running build. `status`, `display status`, `verbose on/off`, `settings show`,
`screen race/camera`, `set ...`, IMU and SD diagnostics work in the normal build.

## Physical settings navigation

RED OFF/ON arms entry; CENTER + GREEN for two seconds opens SETTINGS only
when the existing actual lap/speed/alarm guards allow it. LEFT/RIGHT + GREEN
moves one row, CENTER + GREEN selects. The switch alone never navigates.
Numeric values retain the 600 ms initial delay, 80 ms/0.05 bar pressure repeat
and 250 ms repeat for other numbers. Direction change or release cancels repeat.
RED OFF exits and discards drafts. Root BACK consumes arming until RED OFF/ON.

BACK is row zero and initially selected in root, every category, nested group,
choice page, numeric entry page, SD FILES and IMU wizard. Returning selects the
originating item in the parent. BACK never maps to a configuration value.
Choice highlighting is red; the saved value is green. Only confirmation saves.
A numeric page first offers BACK / EDIT VALUE. Select EDIT VALUE before changing
numbers. Pressure confirms MIN, then MAX, then saves the complete valid pair.

Root: BACK, DISPLAY, CALIBRATION, UNITS, TRACK, TPMS, STORAGE.
DISPLAY groups QUICK INFO (QUICK INFO 1/2/3) and LAP DELTA (SECONDS PER LIGHT).
Quick pages and their five-second return are preserved. They do not configure
comparison lights. `quickSlots` and `deltaIndex` retain their NVS v4 meaning.

Quick tokens: `last_lap`, `rear_pressure`, `front_pressure`, `best_lap`,
`max_angle`, `max_speed`, `max_acceleration`, `max_deceleration`.
Themes: `black`, `white`, `blue`. Commands: `set quick1 TOKEN` (also quick2/3),
`set background THEME`, `max reset` (current source maxima).

CAMERA keeps the main timer, pressures, speed and incremental lean bar.
Before calibration, a fresh accelerometer estimate is explicitly marked RAW;
it does not validate the dynamic estimate or IMU-derived maxima. Calibration
and the user's separate lean zero still apply. Stale/lost IMU remains invalid.

## Development-only controls

All `test ...`, `button ...`, `key ...`, `switch ...`, `selftest` and `sd selftest`
commands below require the development build. Physical guards still use REAL
lap and speed in TEST. `switch settings on` and `key` are direct diagnostic
controls; use `button` to exercise actual entry protection.

```text
button red off
button direction center
button red on
button green down
```

Wait at least two seconds, then `button green up`. The selected row is BACK.
Move with `button direction right` then GREEN down/up per row; use direction
CENTER and a fresh GREEN down/up to select. Finish with `button red off`.

`test on/off/reset` isolates synthetic data, references and maxima from REAL.
`test pressure front/rear BAR`, `test sensor front/rear lost`,
`test motion ANGLE_DEG SPEED_KMH ACCEL_G` and `test tick MILLISECONDS`
provide explicitly labelled inputs. Changing TEST never replays physical GPS.

Focused settings check without board, calibration or SD writes:

```powershell
.\.venv\Scripts\python.exe tools/test_storage_imu.py --suite settings
```

It checks BACK, QUICK INFO, preserved values, numeric-repeat gating and exact
positive/negative light thresholds. It can also run as `selftest settings`
in the development firmware. The retained broader `selftest`,
`selftest storage-imu`, `tools/test_panel.py` and `tools/test_terminal.py`
are optional development checks; do not rerun hardware suites for a UI cleanup.
The scripts need the development build. The host helper accepts a C++ compiler
on PATH, or Zig installed under `.pio/host-tools` (instructions in the script).

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
for the next lap. Three display levels are independent of the three timing
sectors. Both sides mirror min(3, floor(abs(delta_ms) / step_ms)), using one
shared calculation. Below one step and without a reference they stay neutral.
Steps are 100/200/500/1000 ms, identical for faster (green) and slower (red) laps.
Delta updates at S1/S2/finish and holds between boundaries; it is not a
continuous prediction from a stored trajectory.

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

`STORAGE > SD FILES` lists five filenames per page with sizes and BACK/NEXT/
PREVIOUS navigation. `sd files [FIRST_INDEX]` prints full paths in the terminal.
The directory walk handles root plus three nested levels and processes one
entry between queued writes, instead of pausing CSV acquisition for a full scan.

`STORAGE > FORMAT SD` opens on BACK, then NO and ERASE ALL FILES. Only the
explicit ERASE ALL FILES choice requests FAT32
formatting. Logging must be OFF and its file closed, with REAL selected, no
active lap/movement/alarm or other format. Formatting deletes all files and SD
best references; successful completion clears the in-memory best reference.
`sd format` only prints confirmation instructions. The destructive terminal
command is `sd format ERASE-ALL-FILES`; do not run it as a read-only check.
The new synthetic tests exercise confirmation and guards without erasing data.

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
the other valid slot; corrupt-slot fallback preserves the other valid slot.
These are separate from session logging. TEST cannot write a production slot.

`test sd slow` delays TEST writes by 200 ms, `test sd fail` fails TEST writes,
`test sd normal` restores them. These commands require TEST ON and do not
inject faults into real reference storage or real files. Reopen logging after
a simulated failure (`set logging off`, `set logging on`).

## IMU calibration and experimental dynamics

Bike axes: +X forward, +Y left, +Z up; acceleration in g, gyro in deg/s.
Rotation rows are these bike axes expressed in sensor coordinates, determinant
+1. Gyro bias, the full rotation and user lean zero are separate quantities.

The current menu uses `CALIBRATION > IMU CALIBRATE` or terminal `imu calibrate`.
Follow UPRIGHT, LEFT, RIGHT prompts. Each step opens on BACK; select MEASURE
with LEFT/RIGHT + GREEN, then CENTER + GREEN (or `imu next`) once the bike is
in the requested pose. BACK safely cancels an unfinished capture. Each capture needs two
seconds; an OK is shown before the next instruction. Tilts can differ, 5..70
degrees each, on opposite sides of the same roll plane. The full rotation and
upright gyro bias are saved only after all three poses pass; old user zero is
then reset. RED OFF or `imu cancel` preserves the previous calibration.

The stability window permits gyro RMS noise up to 3 deg/s, acceleration-norm
standard deviation up to 0.06 g, and up to 3 degrees of smoothed orientation
drift. Mean rotation after the upright bias capture must stay below 3 deg/s;
upright capture permits mean gyro bias up to 8 deg/s. Mean gravity must be
within 0.18 g of 1 g. At least 60 samples and two seconds of continuous data
are required; gaps over 120 ms restart capture. Failed windows retry.

In the development build, `selftest storage-imu` runs synthetic SD/IMU assertions; it never
formats a card or writes NVS. Without a board, the same C++ tests run via
`python tools/test_storage_imu.py` with a host C++ compiler (see script).

Legacy manual commands remain available for diagnostics:

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
