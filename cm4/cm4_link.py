#!/usr/bin/env python3
"""
Run this ON the CM4 to talk to the RISC-V core's console over the CM4's own
hardware UART (GPIO14/15 -- the same pins a full-size Raspberry Pi uses for
its primary serial port), instead of a USB-to-TTL adapter.

Requires the FPGA bitstream to have been built with --with-cm4-uart (see
litex-soc/qmtech_kintex7_devboard.py), and the CM4's UART enabled and freed
from the getty/Bluetooth that normally claim it -- see cm4/README.md for the
/boot/config.txt changes needed first.

This is intentionally a plain pyserial passthrough, not litex_term -- it
doesn't understand the BIOS's serialboot handshake. For that, either run
litex_term directly on the CM4 (same package as on the dev machine, works
identically), or use this script for a quick manual poke at the console.
"""
import sys
import serial

PORT = "/dev/serial0"
BAUD = 115200


def main():
    try:
        ser = serial.Serial(PORT, BAUD, timeout=0.1)
    except serial.SerialException as e:
        print(f"Couldn't open {PORT}: {e}")
        print("Check /boot/config.txt has enable_uart=1 and the Bluetooth")
        print("overlay disabled -- see cm4/README.md.")
        sys.exit(1)

    print(f"Connected to {PORT} at {BAUD} baud. Ctrl-C to exit.")
    ser.write(b"\r\n")  # nudge the BIOS/firmware to reprint its prompt

    try:
        while True:
            data = ser.read(256)
            if data:
                sys.stdout.buffer.write(data)
                sys.stdout.flush()
    except KeyboardInterrupt:
        print("\nDisconnected.")


if __name__ == "__main__":
    main()
