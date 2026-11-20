"""Send console commands over the serial port and capture the log, for scripted bench work.

Opens the port without toggling DTR/RTS (which would reset the board), listens for a moment, then sends each command and
records what the firmware prints after it. Needs pyserial (the ESP-IDF Python environment has it); close any monitor
first.

    python tools/console.py COM6 --listen 3
    python tools/console.py COM6 --wait 2 "state" "sdo r 2000 07"
    python tools/console.py COM6 --wait 8 "dump" --output dump.txt
"""

import argparse
import sys
import time

import serial


def open_port(port_name):
    port = serial.Serial()
    port.port = port_name
    port.baudrate = 921600
    port.timeout = 0.1
    port.dtr = False
    port.rts = False
    port.open()
    return port


def capture(port, seconds, out):
    deadline = time.time() + seconds
    buffer = bytearray()

    while time.time() < deadline:
        chunk = port.read(4096)

        if chunk:
            buffer += chunk

    text = buffer.decode("utf-8", "replace")
    out.write(text)
    out.flush()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("port")
    parser.add_argument("commands", nargs="*")
    parser.add_argument("--listen", type=float, default=0.5, help="seconds to listen before the first command")
    parser.add_argument("--wait", type=float, default=2.0, help="seconds to capture after each command")
    parser.add_argument("--output", default=None)
    args = parser.parse_args()

    out = open(args.output, "w", encoding="utf-8") if args.output else sys.stdout
    port = open_port(args.port)

    try:
        capture(port, args.listen, out)

        for command in args.commands:
            out.write(f"\n>>> {command}\n")
            port.write((command + "\n").encode())
            capture(port, args.wait, out)
    finally:
        port.close()

        if args.output:
            out.close()


if __name__ == "__main__":
    main()
