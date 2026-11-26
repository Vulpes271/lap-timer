"""Check the app's wire contract and on-device HTTP round trips over USB."""
import argparse
import json
import re
import time
from pathlib import Path

import serial


def validate(index, payload, fixture):
    assert index["version"] == 1 and index["storageError"] is False
    assert index["device"].startswith("TEST-")
    assert index["sessions"] == [fixture["session"]]
    assert payload == {"session": fixture["session"], "laps": fixture["laps"]}
    summary = payload["session"]
    assert summary["id"] > 0 and summary["number"] == 1
    assert summary["startUtcMs"] > 946684800000
    assert summary["lapCount"] == len(payload["laps"]) == 8
    for number, lap in enumerate(payload["laps"], 1):
        assert lap["number"] == number
        assert len(lap["sectorsMs"]) == 3 and min(lap["sectorsMs"]) > 0
        assert abs(sum(lap["sectorsMs"]) - lap["lapMs"]) <= 2
    assert min(lap["lapMs"] for lap in payload["laps"]) == 63800


def request(connection, command, prefix, timeout=8):
    connection.write((command + "\n").encode("ascii"))
    connection.flush()
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        line = connection.readline().decode("utf-8", errors="replace").strip()
        if line:
            print(line, flush=True)
        if line.startswith(prefix):
            return line[len(prefix):]
    raise RuntimeError(f"No {prefix!r} response to {command!r}")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", default="COM3")
    parser.add_argument("--fixture-only", action="store_true", help="Validate generated data without a connected ESP32")
    args = parser.parse_args()
    fixture = json.loads(Path(__file__).with_name("demo-session.json").read_text(encoding="utf-8-sig"))
    if args.fixture_only:
        header = Path(__file__).with_name("include").joinpath("demo_data.h").read_text(encoding="utf-8-sig")
        session, laps = [json.loads(value) for value in re.findall(r'R"json\((.*?)\)json"', header)]
        validate({"version": 1, "device": "TEST-FIXTURE", "storageError": False, "sessions": [session]},
                 {"session": session, "laps": laps}, fixture)
        print("PASS: generated firmware data matches the fixture and Android session/lap contract.")
        return
    # Set control lines before opening; avoid unintentionally resetting this board.
    connection = serial.Serial(port=None, baudrate=115200, timeout=0.25)
    connection.dtr = False
    connection.rts = False
    connection.port = args.port
    with connection:
        time.sleep(1)
        connection.reset_input_buffer()
        assert "wifi=ON" in request(connection, "status", "STATUS "), "Wi-Fi is not running"
        index = json.loads(request(connection, "sessions", "JSON_SESSIONS "))
        payload = json.loads(request(connection, "laps", "JSON_LAPS "))
        validate(index, payload, fixture)
        result = json.loads(request(connection, "selftest", "SELFTEST ", timeout=35))
        assert result["passed"] is True, result
        request(connection, "depart", "DEPART ")
        assert "wifi=OFF" in request(connection, "status", "STATUS ")
        request(connection, "return", "PHONE: ")
        assert "wifi=ON" in request(connection, "status", "STATUS ")
        again = json.loads(request(connection, "sessions", "JSON_SESSIONS "))
        assert again == index  # Returning must not create a duplicate session.
    print("PASS: one session, eight valid laps, HTTP routes, error responses and departure/return.")


if __name__ == "__main__":
    main()
