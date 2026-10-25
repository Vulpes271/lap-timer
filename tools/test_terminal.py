"""Exercise the real ESP32 terminal, restoring user settings in finally.

Requires pyserial in the project venv. Writes TEST SD files and an isolated NVS
selftest key. No physical calibration, GPIO changes or real timing injection.
"""
import argparse
import json
from pathlib import Path
import re
import time
import serial


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="COM4")
    args = parser.parse_args()
    port = serial.Serial()
    port.port, port.baudrate, port.timeout = args.port, 115200, 0.1
    port.dtr = port.rts = False
    port.open()
    log = []
    passed = 0

    def read_until(predicate, timeout=10):
        output = ""
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            output += port.read(16384).decode(errors="replace")
            if predicate(output):
                time.sleep(0.12)
                output += port.read(16384).decode(errors="replace")
                log.append(output)
                return output
            if "Guru Meditation" in output or "Stack canary" in output:
                raise AssertionError(output)
        log.append(output)
        raise AssertionError(f"Timeout: {output}")

    def command(text, marker="OK", timeout=10):
        log.append(f"> {text}\n")
        port.write((text + "\n").encode())
        return read_until(lambda s: marker in s, timeout)

    def check(condition, name):
        nonlocal passed
        if not condition:
            raise AssertionError(name)
        passed += 1
        print(f"PASS {name}", flush=True)

    def status():
        return command("status", "SD mounted=")

    def value(text, key):
        match = re.search(rf"\b{key}=([^\s]+)", text)
        assert match, (key, text)
        return match[1]

    time.sleep(1)
    port.reset_input_buffer()
    original = None
    try:
        initial = status()
        config_line = next(line for line in initial.splitlines() if line.startswith("SETTINGS "))
        original = dict(re.findall(r"(\w+)=([^\s]*)", config_line))
        quick_line=next(line for line in initial.splitlines() if line.startswith("QUICK SETTINGS "))
        original.update(dict(re.findall(r"(\w+)=([^\s]*)",quick_line)))
        Path(".platformio/terminal_settings_before.json").write_text(json.dumps(original, indent=2))
        result = command("selftest", "CONTROL TEST RESULT:", 25)
        check("TIMING TEST RESULT: 20 passed, 0 failed" in result, "existing GPS timing suite")
        check(re.search(r"CONTROL TEST RESULT: \d+ passed, 0 failed", result), "control, NVS, units and parser suite")
        command("test off")
        for text in ["set quick1 last_lap","set quick2 rear_pressure","set quick3 front_pressure","set background black"]:
            command(text)
        check("ERR" in command("test lap 90000", "ERR"), "test injection gated")
        response = command("switch settings on")
        check("LEGACY" not in response and "settings=1" in response, "word command does not dispatch legacy letters")
        command("switch direction center")
        command("switch direction right")
        first = status()
        command("switch direction right")
        repeated = status()
        check(value(first, "menu") == value(repeated, "menu"), "repeated switch state makes no edge")
        command("switch settings off")
        command("screen camera")
        check("base=CAMERA" in status(), "CAMERA selection")
        command("screen race")
        command("test on")
        command("test lap 90000")
        check("view=6" in status(), "ordinary lap hold")
        command("test lap 88000")
        check("view=7" in status(), "BEST overlay")
        command("key quick")
        check("view=3" in status(), "quick covers BEST")
        command("test tick 6000")
        command("test motion -44 180 0.55")
        command("test motion 40 170 -0.80")
        peaks=status()
        check("angle_deg=44.000" in peaks and "speed_kmh=180.000" in peaks and "deceleration_g=0.8000" in peaks,"independent TEST session maxima")
        choices=["last_lap","rear_pressure","front_pressure","best_lap","max_angle","max_speed","max_acceleration","max_deceleration"]
        expected=[3,4,5,9,10,11,12,13]
        for choice,view in zip(choices,expected):
            command(f"set quick1 {choice}")
            command("screen race")
            command("key quick")
            check(value(status(),"view")==str(view),f"configurable QUICK page: {choice}")
            clean=command("v","TFT QUICK CLEAN:")
            check("TFT QUICK CLEAN: PASS" in clean,f"minimal footer-free display: {choice}")
        command("set quick1 last_lap")
        command("screen race")
        for theme in ["black","white","blue"]:
            command(f"set background {theme}")
            rendered=command("v","TFT RAM: text")
            check("FAIL" not in rendered and rendered.count("PASS")>=2,f"theme {theme}: actual TFT text/background readback")
        command("set background black")
        check("view=0" in status(), "expired quick and covered BEST stay expired")
        command("test sector 1 28000")
        check(float(value(status(), "delta")) < 0, "sector compares to full reference")
        command("test sector 2 57000")
        command("test lap 87000")
        command("test tick 6000")
        command("set alarm off")
        for text in ["set front_range 2.0 3.0", "set rear_range 2.0 3.0", "set front_id SIM-F", "set rear_id SIM-R", "set confirm_ms 1000", "set hysteresis 0.05", "set stale_ms 120000", "set alarm on"]:
            command(text)
        command("test pressure front 1.70")
        command("test pressure rear 3.50")
        command("test tick 1100")
        command("test pressure front 1.70")
        command("test pressure rear 3.50")
        text = status()
        check("view=8" in text and "alarm_low=1/0" in text and "alarm_high=1/0" in text, "both front LOW and rear HIGH")
        command("key quick")
        check("view=0" in status(), "alarm acknowledgement returns RACE")
        command("test pressure front 1.70")
        check("view=8" not in status(), "acknowledged persistent alarm does not reopen")
        for text in ["test pressure front 2.50", "test pressure rear 2.50", "test tick 1100", "test pressure front 2.50", "test pressure rear 2.50", "test pressure front 1.70", "test tick 1100", "test pressure front 1.70"]:
            command(text)
        check("view=8" in status(), "hysteresis recovery rearms alarm")
        command("key quick")
        command("test sensor front lost")
        check("TPMS F id=SIM-F status=SENSOR LOST" in status(), "lost sensor distinct from pressure LOW")
        command("set pressure psi")
        command("settings reload")
        text = status()
        check("pressure=1" in text and "front=2.000/3.000" in text, "unit survives NVS reload, limits remain bar")
        if "SD mounted=1" in initial:
            check("SD READBACK: PASS" in command("sd readback", "SD READBACK:"), "SD explicit readback diagnostic")
            storage = command("sd selftest", "SD REFERENCE TEST:")
            check("SD REFERENCE TEST: PASS" in storage, "SD reference reload and corrupt-slot fallback")
            command("set logging on")
            time.sleep(0.7)
            text = status()
            check("active=1" in text and "file=/test/" in text, "separate TEST log file")
            before_dropped = int(value(text, "dropped"))
            command("test sd slow")
            for _ in range(10):
                log.append("> bulk 10 TEST pressure packets\n")
                port.write(b"test pressure rear 2.50\n" * 10)
                read_until(lambda s: s.count("OK source=") >= 10)
            command("test sd normal")
            text = status()
            check(int(value(text, "dropped")) > before_dropped, "slow SD queue overflow counted without stopping UI")
            time.sleep(1)
            text = status()
            errors = int(value(text, "errors"))
            command("test sd fail")
            command("test pressure rear 2.50")
            time.sleep(0.4)
            text = status()
            check(int(value(text, "errors")) > errors and "source=TEST" in text, "injected SD failure leaves controller alive")
            command("test sd normal")
            command("set logging off")
        command("test off")
        text = status()
        check("source=REAL" in text and "last=87000" not in text and "best=87000" not in text, "TEST OFF restores real timing")
        if "SD mounted=1" in initial:
            command("set logging on")
            command("screen camera")
            time.sleep(1.5)
            command("screen race")
            time.sleep(1.5)
            inspected = command("sd inspect", "SD INSPECT:")
            check("file=/sessions/" in inspected and int(value(inspected, "imu_rows")) > 0 and int(value(inspected, "gps_rows")) > 0, "actual GPS and raw IMU stored in REAL CSV")
            check(int(value(inspected, "imu_max_us")) < 60000, "IMU acquisition continues across screen redraws")
            command("set logging off")
        check("ERR" in command("not_a_command", "ERR"), "unknown command rejected")
        check("ERR" in command("x" * 220, "ERR"), "oversized line rejected")
        check("source=REAL" in status(), "parser recovers after overflow")
        print(f"TERMINAL INTEGRATION RESULT: {passed} passed", flush=True)
    finally:
        if original:
            # Restore only settings owned by this harness, in validation-safe order.
            try:
                command("test off")
                command("switch settings off")
                command("set alarm off")
                for wheel in ("front", "rear"):
                    lower, upper = original[wheel].split("/")
                    command(f"set {wheel}_range {lower} {upper}")
                    command(f"set {wheel}_id {original[wheel+'_id'] or 'none'}")
                for key, original_key in [("hysteresis", "hysteresis"), ("confirm_ms", "confirm"), ("stale_ms", "stale")]:
                    command(f"set {key} {original[original_key]}")
                unit = ["bar", "psi", "kpa"][int(original["pressure"])]
                command(f"set pressure {unit}")
                command("set alarm " + ("on" if original["alarm"] == "1" else "off"))
                quick_names=["last_lap","rear_pressure","front_pressure","best_lap","max_angle","max_speed","max_acceleration","max_deceleration"]
                for slot in range(1,4):
                    command(f"set quick{slot} {quick_names[int(original['quick'+str(slot)])]}")
                command("set background "+["black","white","blue"][int(original["theme"])])
                command("set logging " + ("on" if original["logging"] == "1" else "off"))
                command("screen " + ("camera" if original["base"] == "1" else "race"))
                print("Original settings restored; source REAL", flush=True)
            except Exception as exc:
                print(f"RESTORE FAILED: {exc}; backup .platformio/terminal_settings_before.json", flush=True)
                raise
        Path("tools/terminal_test.log").write_text("\n".join(log), encoding="utf-8")
        port.close()


if __name__ == "__main__":
    main()
