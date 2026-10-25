"""Test guarded panel navigation on a stationary ESP32; leave switches untouched.

No motion is injected. REAL lap/speed guards remain active. Confirmed settings
are left unchanged; tests only change a draft and confirm an unchanged value.
"""
import argparse
import re
import sys
import time
from pathlib import Path
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

    def command(text, marker="OK source="):
        log.append(f"> {text}\n")
        port.write((text + "\n").encode())
        output = ""
        end = time.monotonic() + 6
        while time.monotonic() < end:
            output += port.read(16384).decode(errors="replace")
            if marker in output:
                log.append(output)
                return output
        log.append(output)
        raise AssertionError(f"Timeout: {text}: {output}")

    def status():
        return command("status", "SD mounted=")

    def field(text, key):
        return re.search(rf"\b{key}=([^\s]+)", text).group(1)

    def check(condition, description):
        nonlocal passed
        assert condition, description
        passed += 1
        print(f"PASS {description}", flush=True)

    def green_short():
        command("button green down")
        command("button green up")

    def green_hold():
        command("button green down")
        time.sleep(2.25)
        result = status()
        command("button green up")
        return result

    def require_menu(text):
        if field(text, "settings") != "1" or field(text, "suppressed") != "0":
            raise RuntimeError("Settings did not stay open: REAL GPS/lap/alarm protection may have blocked entry or closed the menu; inspect PANEL motion fields and the log")
        return text

    def config(text):
        return tuple(line for line in text.splitlines()
                     if line.startswith(("SETTINGS ", "QUICK SETTINGS ")))

    def direction(value):
        command("button direction " + value)

    def move(right, count=1):
        direction("right" if right else "left")
        for _ in range(count):
            green_short()
        direction("center")

    try:
        time.sleep(1)
        port.reset_input_buffer()
        initial = status()
        assert field(initial, "source") == "REAL", "Use REAL mode for this test"
        assert field(initial, "settings") == "0", "Close settings before testing"
        assert field(initial, "green") == "0", "Release the physical GREEN button"
        assert field(initial, "lap_block") == "0", "Active real lap blocks this test"
        assert field(initial, "speed_fresh") == "0" or float(field(initial, "speed_kmh")) <= 5, "Real GPS motion blocks entry"
        original = config(initial)
        command("button red off")
        command("button green up")
        direction("center")
        command("button red on")
        check(field(status(), "settings") == "0", "red alone does not open settings")
        entered = require_menu(green_hold())
        check(field(entered, "settings") == "1" and field(entered, "editing") == "0", "center green hold opens categories")
        direction("right")
        check(field(status(), "row") == "0", "direction switch alone does not navigate")
        green_short()
        green_short()
        check(field(status(), "row") == "2", "held RIGHT plus green clicks advances repeatedly")
        require_menu(green_hold())
        check(field(status(), "row") == "3" and field(status(), "depth") == "0", "holding green on list advances only one row")
        direction("left")
        green_short()
        check(field(status(), "row") == "2", "next press uses reversed direction without recentering")
        direction("center")
        green_short()
        check(field(status(), "depth") == "1" and field(status(), "category").startswith("2:"), "center green opens UNITS")
        green_short()
        opened = status()
        cursor = int(field(opened, "editor_row"))
        check(field(opened, "editing") == "1" and field(opened, "choices") == "2", "setting opens separate option page")
        move(True)
        check(int(field(status(), "editor_row")) == (cursor + 1) % 3 and config(status()) == original, "direction green selects option without saving")
        move(False)
        green_short()
        confirmed = status()
        check(field(confirmed, "editing") == "0" and field(confirmed, "confirmed") == "1" and config(confirmed) == original, "center green saves unchanged option and returns")
        move(False)
        green_short()
        check(field(status(), "depth") == "0", "submenu BACK returns to categories")
        move(False, 2)
        green_short()
        move(True, 4)
        green_short()
        opened = status()
        cursor = int(field(opened, "editor_row"))
        check(field(opened, "choices") == "3" and field(opened, "editing") == "1", "BACKGROUND opens color choices")
        direction("right")
        green_hold()
        check(int(field(status(), "editor_row")) == (cursor + 1) % 4, "color hold advances once without auto-repeat")
        direction("center")
        cursor = int(field(status(), "editor_row"))
        move(True, (3 - cursor) % 4)
        green_short()
        check(field(status(), "editing") == "0" and config(status()) == original, "color BACK cancels and returns without changing settings")
        green_short()
        green_short()
        check(field(status(), "confirmed") == "1" and config(status()) == original, "color confirmation returns with green status")
        command("button red off")
        command("button red on")
        require_menu(green_hold())
        move(True, 4)
        green_short()
        move(True)
        green_short()
        opened = status()
        check(field(opened, "numeric") == "1" and field(opened, "range_part") == "0", "pressure opens numeric MIN editor")
        factor = [1.0, 14.503773773, 100.0][int(field(initial, "pressure"))]
        start_value = float(field(opened, "value")) / factor
        if start_value <= 18:
            direction("right")
            started = time.monotonic()
            command("button green down")
            time.sleep(4.0)
            held = status()
            command("button green up")
            elapsed = time.monotonic() - started
            new_value = float(field(held, "value")) / factor
            check(new_value - start_value >= 1.99 and elapsed < 5.0,
                  f"pressure rises at least 2 bar within 5 seconds ({new_value-start_value:.2f} bar/{elapsed:.2f}s)")
            check(field(status(), "editing") == "1" and config(status()) == original, "pressure repeat never commits on release")
        command("button red off")
        check(config(status()) == original, "RED OFF discards numeric draft and preserves confirmed settings")
        command("button red on")
        direction("center")
        require_menu(green_hold())
        move(False)
        green_short()
        check(field(status(), "suppressed") == "1", "root BACK exits to RACE")
        check(field(green_hold(), "suppressed") == "1", "red remaining ON cannot reopen after BACK")
        command("button red off")
        command("button red on")
        check(field(green_hold(), "settings") == "1", "RED OFF ON permits fresh entry")
        command("button red off")
        print(f"PANEL INTEGRATION RESULT: {passed} passed, 0 failed", flush=True)
    finally:
        already_failed = sys.exc_info()[0] is not None
        cleanup_errors = []
        try:
            for text in ("button green up", "button red off", "button direction center"):
                try:
                    command(text)
                except Exception as exc:
                    cleanup_errors.append(str(exc))
        finally:
            port.close()
            Path(".platformio/panel_integration.log").write_text("".join(log), encoding="utf-8")
        if cleanup_errors:
            print("Panel cleanup incomplete: " + "; ".join(cleanup_errors), flush=True)
            if not already_failed:
                raise RuntimeError("Panel cleanup incomplete; release contacts and reset RED OFF")


if __name__ == "__main__":
    main()
