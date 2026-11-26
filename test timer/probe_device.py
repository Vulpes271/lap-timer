"""Send a simulator command and read the USB log without asserting reset lines."""
import argparse
import time

import serial


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("command", nargs="?", default="status")
    parser.add_argument("--port", default="COM3")
    parser.add_argument("--seconds", type=float, default=8)
    args = parser.parse_args()
    connection = serial.Serial(port=None, baudrate=115200, timeout=0.25)
    connection.dtr = False
    connection.rts = False
    connection.port = args.port
    with connection:
        time.sleep(1)
        connection.write((args.command + "\n").encode("ascii"))
        until = time.monotonic() + args.seconds
        while time.monotonic() < until:
            line = connection.readline().decode("utf-8", errors="replace").strip()
            if line:
                print(line, flush=True)


if __name__ == "__main__":
    main()
