# CM4 integration

Prep work for pairing the Raspberry Pi Compute Module 4 with the FPGA,
written before the CM4's eMMC was actually flashed -- so treat the exact
steps below as a plan to verify once it's up, not a tested procedure.

## The link: a second UART on GPIO14/15

The FPGA side is built and tested (see `../litex-soc/`, built with
`--with-cm4-uart`): a second, independent UART peripheral on GPIO14/TX and
GPIO15/RX -- the same physical pins a full-size Raspberry Pi uses for its
own primary serial port. Once the CM4 is running Linux, its own hardware
UART can talk directly to the RISC-V core's console over these pins, no
USB-to-TTL adapter needed on that side (that adapter is only standing in for
the CM4 until it's ready).

### CM4-side setup (untested until the CM4 is up)

1. Edit `/boot/firmware/config.txt` (or `/boot/config.txt` on older Raspberry
   Pi OS releases):
   ```
   enable_uart=1
   dtoverlay=disable-bt
   ```
   (`disable-bt` matters -- without it, the CM4's primary PL011 UART is
   claimed by Bluetooth and only the much less reliable "mini UART" is
   exposed as the serial port.)
2. Stop the login shell from claiming the port: `sudo raspi-config` ->
   Interface Options -> Serial Port -> "login shell over serial" = No,
   "serial port hardware enabled" = Yes. (Equivalently: remove any
   `console=serial0,...` from `/boot/firmware/cmdline.txt` and
   `sudo systemctl disable --now serial-getty@ttyAMA0.service`.)
3. Reboot, then `/dev/serial0` should exist and be free to use.
4. Either run `litex_term /dev/serial0 --serial-boot --kernel <path>` on the
   CM4 directly (same package, works identically to running it on the dev
   machine), or use `cm4_link.py` here for a quick manual poke at the
   console.

### Files here

- `cm4_link.py` -- minimal pyserial passthrough to the FPGA's console over
  `/dev/serial0`. Run on the CM4.
- `flash_fpga.sh` -- **confirmed blocked** as of writing (see the comment
  in the script) -- the CM4 can't currently reflash the FPGA itself, because
  Vivado Lab Edition (needed for this project's specific JTAG cable) has no
  ARM64 build, and openFPGALoader/xc3sprog don't work with this cable
  regardless of architecture. Kept for when either a working plain-FTDI
  cable replaces the current one, or upstream JTAG tooling adds support for
  this one.

## GPIO pin reference

The QMTech carrier routes the CM4's GPIO0-27 straight to specific FPGA
balls (same numbering as a real Raspberry Pi header) -- see the `"GPIO"`
connector table in `../litex-soc/qmtech_kintex7_devboard_platform.py` for
the full mapping if you want to wire up something beyond the UART pins.

## Still to do once the CM4 is actually up

- Flash Raspberry Pi OS to the eMMC (see the main project history / session
  notes -- needs `rpiboot` in USB mass-storage-gadget mode, and finding the
  carrier's `nRPIBOOT` jumper, which wasn't located as of writing since the
  eMMC flash hadn't happened yet)
- Verify the UART link actually works end to end (steps above are untested)
- Decide what the CM4 is actually *for* once connected -- network-facing
  dashboard, bitstream library/switcher, or something else. Not designed
  yet.
