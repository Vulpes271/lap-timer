import argparse
import time

import serial


def main() -> None:
    parser = argparse.ArgumentParser(description="Read ESP32 serial output briefly.")
    parser.add_argument("--port", default="COM3")
    parser.add_argument("--seconds", type=float, default=6.0)
    args = parser.parse_args()

    with serial.Serial(args.port, 115200, timeout=0.5) as connection:
        connection.dtr = False
        connection.rts = False
        deadline = time.monotonic() + args.seconds

        while time.monotonic() < deadline:
            line = connection.readline().decode(errors="replace").strip()
            if line:
                print(line, flush=True)


if __name__ == "__main__":
    main()
