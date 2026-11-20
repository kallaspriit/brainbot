"""Capture the console's display to a PNG over the serial port.

Sends "screenshot" to the firmware, reads the framed run-length encoded RGB565 image it answers with, checks the CRC
and writes screenshots/<timestamp>.png. Needs only pyserial, which the ESP-IDF Python environment has.

    python tools/screenshot.py COM6
    python tools/screenshot.py COM6 --output home.png
    python tools/screenshot.py COM6 --open home overview velocity   (one PNG per screen)
"""

import argparse
import struct
import sys
import time
import zlib
from datetime import datetime
from pathlib import Path

import serial

BAUD_RATE = 921600  # CONFIG_ESP_CONSOLE_UART_BAUDRATE in firmware/sdkconfig.defaults
REPLY_TIMEOUT_S = 10  # Rendering and encoding take well under a second; the rest is margin
TRANSFER_TIMEOUT_S = 60
SCREEN_OPEN_DELAY_S = 0.6  # Opening is deferred to the next LVGL cycle, then the screen has to render
SCREENSHOTS_DIRECTORY = Path(__file__).resolve().parent.parent / "screenshots"


def open_port(port_name):
    """Open the port without toggling DTR/RTS, which would reset the board through the auto-reset circuit."""
    port = serial.Serial()
    port.port = port_name
    port.baudrate = BAUD_RATE
    port.timeout = 0.2
    port.dtr = False
    port.rts = False
    port.open()

    return port


def read_line(port, deadline):
    """Read one line, ignoring log output, until the deadline."""
    line = bytearray()

    while time.time() < deadline:
        character = port.read(1)

        if not character:
            continue

        if character == b"\n":
            return line.decode("utf-8", "replace").strip()

        line += character

    raise TimeoutError("No reply from the console")


def read_exactly(port, length, deadline):
    """Read a fixed number of bytes until the deadline."""
    data = bytearray()

    while len(data) < length:
        if time.time() > deadline:
            raise TimeoutError(f"Transfer stalled at {len(data)} of {length} bytes")

        data += port.read(length - len(data))

    return bytes(data)


def decode_run_length(payload, width, height):
    """Decode (run length, RGB565 pixel) pairs into rows of RGB bytes."""
    pixels = bytearray()

    for offset in range(0, len(payload), 4):
        run_length, pixel = struct.unpack_from("<HH", payload, offset)
        red = ((pixel >> 11) & 0x1F) * 255 // 31
        green = ((pixel >> 5) & 0x3F) * 255 // 63
        blue = (pixel & 0x1F) * 255 // 31
        pixels += bytes((red, green, blue)) * run_length

    expected_length = width * height * 3

    if len(pixels) != expected_length:
        raise ValueError(f"Decoded {len(pixels)} bytes, expected {expected_length}")

    return pixels


def write_png(path, pixels, width, height):
    """Write 8-bit RGB pixels as a PNG, with the standard library only."""

    def chunk(chunk_type, data):
        return struct.pack(">I", len(data)) + chunk_type + data + struct.pack(">I", zlib.crc32(chunk_type + data))

    row_length = width * 3
    rows = b"".join(b"\x00" + pixels[y * row_length : (y + 1) * row_length] for y in range(height))
    header = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)

    with open(path, "wb") as file:
        file.write(b"\x89PNG\r\n\x1a\n")
        file.write(chunk(b"IHDR", header))
        file.write(chunk(b"IDAT", zlib.compress(rows, 9)))
        file.write(chunk(b"IEND", b""))


def capture(port_name, output_path, screen_name=None):
    """Ask the firmware for a screenshot and save it, optionally opening a screen first."""
    started = time.time()

    with open_port(port_name) as port:
        port.reset_input_buffer()

        if screen_name:
            port.write(f"open {screen_name}\n".encode())
            time.sleep(SCREEN_OPEN_DELAY_S)
            port.reset_input_buffer()

        port.write(b"screenshot\n")

        # Skip log lines until the frame header
        reply_deadline = time.time() + REPLY_TIMEOUT_S

        while True:
            line = read_line(port, reply_deadline)

            if line.startswith("SCREENSHOT_BEGIN"):
                break

        _, width, height, encoding, payload_length = line.split()
        width, height, payload_length = int(width), int(height), int(payload_length)

        if encoding != "rgb565-rle":
            raise ValueError(f"Unknown encoding {encoding}")

        transfer_deadline = time.time() + TRANSFER_TIMEOUT_S
        payload = read_exactly(port, payload_length, transfer_deadline)

        # The trailer follows after a newline
        trailer = ""

        while not trailer.startswith("SCREENSHOT_END"):
            trailer = read_line(port, transfer_deadline)

    expected_crc = int(trailer.split()[1], 16)
    actual_crc = zlib.crc32(payload)

    if actual_crc != expected_crc:
        raise ValueError(f"CRC mismatch: got {actual_crc:08x}, expected {expected_crc:08x}")

    pixels = decode_run_length(payload, width, height)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    write_png(output_path, pixels, width, height)

    print(f"{output_path} ({width}x{height}, {payload_length} bytes, {time.time() - started:.1f} s)")


def main():
    parser = argparse.ArgumentParser(description="Capture the console's display to a PNG over the serial port.")
    parser.add_argument("port", help="Serial port of the board's USB TO UART connector, e.g. COM6")
    parser.add_argument("--output", type=Path, help="PNG path (default: screenshots/<timestamp>.png)")
    parser.add_argument("--open", nargs="+", metavar="SCREEN", help="Open each screen and save screenshots/<screen>.png")
    arguments = parser.parse_args()

    try:
        if arguments.open:
            for screen_name in arguments.open:
                capture(arguments.port, SCREENSHOTS_DIRECTORY / f"{screen_name}.png", screen_name)
        else:
            output_path = arguments.output or SCREENSHOTS_DIRECTORY / f"{datetime.now():%Y-%m-%d_%H-%M-%S}.png"
            capture(arguments.port, output_path)
    except (TimeoutError, ValueError, serial.SerialException) as error:
        print(f"Screenshot failed: {error}", file=sys.stderr)
        sys.exit(1)


if __name__ == "__main__":
    main()
