# CM4 integration

Prep work for pairing the Raspberry Pi Compute Module 4 with the FPGA,
written before the CM4's eMMC was actually flashed -- so treat the exact
steps below as a plan to verify once it's up, not a tested procedure.

## The link: a second UART on GPIO14/15

**Status: prototyped, built, flashed, and found broken -- do not use
`--with-cm4-uart` yet.** The idea: a second, independent UART peripheral on
GPIO14/TX and GPIO15/RX (`../litex-soc/`, the `--with-cm4-uart` flag) --
the same physical pins a full-size Raspberry Pi uses for its own primary
serial port, so the CM4's own hardware UART could talk directly to the
RISC-V core's console once it's running Linux, no USB-to-TTL adapter needed
on that side.

It builds clean and the interrupt/CSR map has no conflicts (`uart`=IRQ0,
`timer0`=IRQ1, `cm4_uart`=IRQ2, no overlap). The BIOS boots fine and prints
its banner/memtest correctly on real hardware. But a firmware `serialboot`
upload over the *existing* debug UART (JP5, unrelated pins) then fails
reproducibly with UART framing errors mid-transfer, and the BIOS gets stuck
echoing error bytes afterward until a hardware reset. Confirmed reproducible
across a physical reset + retry, so this isn't USB-cable flakiness.

Leading theory, unconfirmed: adding the second UART peripheral shifted
place&route enough to erode timing margin somewhere in the design, and
`nextpnr-xilinx`'s timing closure is known to be less rigorous than
Vivado's (this project has hit that gap before, see the main README's DDR3
section). That would explain why simple interactive text (banner, memtest
output) is unaffected but a fast, sustained binary upload isn't. Reverted
to the known-stable bitstream (without this flag) to keep the board
working; this needs a clear-headed pass with real tools (ideally scoping
the actual UART signal during a failed transfer) before re-enabling it.

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

## Still to do

- **Root-cause the serial upload corruption** described above before relying
  on `--with-cm4-uart` for anything
- Flash Raspberry Pi OS to the eMMC (see the main project history / session
  notes -- needs `rpiboot` in USB mass-storage-gadget mode, and finding the
  carrier's `nRPIBOOT` jumper, which wasn't located as of writing since the
  eMMC flash hadn't happened yet)
- Verify the UART link actually works end to end once both the CM4 is up
  and the corruption bug above is fixed
- Decide what the CM4 is actually *for* once connected -- network-facing
  dashboard, bitstream library/switcher, or something else. Not designed
  yet.
