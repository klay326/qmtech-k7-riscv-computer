# QMTech Kintex-7 RISC-V Computer

Bring-up and demo projects for a **QMTech XC7K325T Kintex-7 development board**,
built entirely through an open-source FPGA toolchain (yosys + nextpnr-xilinx +
prjxray), with flashing handled by AMD's free **Vivado Lab Edition** (no
license required, no synthesis capability needed).

The bigger goal this project is working towards: pairing this FPGA with a
Raspberry Pi Compute Module 4 to build a small heterogeneous computer -- an
ARM Linux side (the CM4) and a RISC-V side (synthesized into the FPGA fabric),
talking to each other.

## Board

- QMTech XC7K325T Kintex-7 core board on their Development Board carrier
- Xilinx Platform Cable USB (clone) for JTAG
- Board manual / schematics: see `ChinaQMTECH/QMTECH_Kintex-7_Development_Board`
  and `ChinaQMTECH/QMTECH_XC7K325T_CORE_BOARD` on GitHub

## Toolchain setup

**Synthesis (fully open source):**
```bash
git clone https://github.com/openXC7/toolchain-installer
cd toolchain-installer
./toolchain-sources-builder.sh   # builds yosys, nextpnr-xilinx, prjxray
source /opt/openxc7/export.sh
```
Note: as of writing, the installer script doesn't correctly build `yosys`
(newer yosys dropped its classic Makefile for CMake) or install prjxray's
Python side (needed for `fasm2frames`) -- see git history / commit notes in
this repo for the exact workarounds if you hit the same issues.

**Flashing:** AMD/Xilinx account required (free) to download **Vivado Lab
Edition** (NOT full Vivado -- it's a much smaller, license-free download that
only does programming/debug, no synthesis). On Arch Linux, the
[`vivado-lab-edition` AUR package](https://aur.archlinux.org/packages/vivado-lab-edition)
handles most of the OS-compatibility patching; alternatively just run the
downloaded `xsetup` installer directly with `--batch Install`.

Programming uses Vivado Lab's Tcl interface (`vivado_lab -mode batch -source
flash.tcl`) rather than `openFPGALoader`, because the specific JTAG cable this
project uses (a Xilinx-VID clone) doesn't correctly implement the low-level
JTAG shift protocol that the open-source reverse-engineered drivers
(openFPGALoader, xc3sprog) expect -- confirmed by both failing identically
against it. The key flag: `connect_hw_server -allow_non_jtag`.

## Structure

```
fpga/
  openXC7.mk               -- shared build rules (from openXC7/demo-projects)
  blinky/                  -- first bring-up: 3 LEDs flashing in sync
  button-chase/            -- LED chase pattern, pause/reverse via onboard buttons
  overclock-experiment/    -- button-controlled runtime CPU overclock (known broken, see its README)
litex-soc/                 -- the LiteX SoC definition (CPU/memory-map/peripherals)
firmware/
  led-chase/               -- bare-metal RISC-V firmware for the LiteX SoC above
```

### FPGA-only demos (`fpga/`)

Each directory is self-contained: `make` builds the bitstream, `vivado_lab
-mode batch -source flash.tcl` programs it (after sourcing Vivado Lab's
`settings64.sh`).

- **blinky** -- clock divider + 3 LEDs, first hardware bring-up
- **button-chase** -- LED chase animation, SW2 pauses/resumes, SW3 reverses
  direction (see the board manual for the actual pin mapping / active-low
  logic)

### RISC-V SoC + firmware (`litex-soc/` + `firmware/led-chase`)

This firmware runs on a [LiteX](https://github.com/enjoy-digital/litex)
SoC (VexRiscv CPU, RV32IM) defined in `litex-soc/` and built via:
```bash
python3 litex-soc/qmtech_kintex7_devboard.py \
    --toolchain=openxc7 --integrated-main-ram-size=0x8000 --build
```
(see `litex-soc/README.md` for the full setup)
(`--integrated-main-ram-size` uses internal block RAM instead of DDR3 --
the DDR3 PHY needs I/O primitives that the open-source Kintex-7 database
doesn't fully cover yet, so this is a deliberate simplification: a RISC-V
computer with 32KB of RAM instead of DDR3's 256MB, until that toolchain gap
is closed or a Vivado Standard/Enterprise license is used for synthesis.)

Build the firmware against that SoC's generated headers:
```bash
cd firmware/led-chase
BUILD_DIR=<path-to-litex-build-dir>/build/qmtech_kintex7_devboard make
```

Load it onto the running SoC over serial (no bitstream rebuild needed for
firmware changes):
```bash
litex_term /dev/ttyUSB0 --kernel main.bin --serial-boot
# then at the BIOS prompt: reboot
```

UART wiring (no onboard USB-serial chip on this board): TX/RX on the JP5
expansion header, ball `AE22`/`AF22` (JP5 pins 7/8), ground shared with the
JTAG header (J1 pin 6) or any other board ground point.

**Commands once loaded:**
- `chase` -- LED chase pattern, same idea as the hardware version but now
  it's software you can edit and reload in seconds
- `donut` -- the classic a1k0n spinning ASCII donut, computed live on the
  RISC-V core
- `pi` -- verifies + continuously stress-tests via the Rabinowitz-Wagon pi
  digit spigot algorithm (integer-only, no FPU needed); memory-bound by the
  small on-chip RAM
- `mandel` -- fixed-point (Q16.16) Mandelbrot set renderer + continuous
  stress test; compute-bound rather than memory-bound, much heavier per
  second than `pi`
- `x86` -- a minimal 8086 instruction interpreter (not the full ISA, just
  the handful of opcodes needed for a small test program) executing a real
  x86 binary assembled from `hello8086.asm` with `nasm -f bin` (verified
  with a native run before porting/embedding as a byte array) -- the
  RISC-V core interpreting genuine x86 machine code, instruction by
  instruction. Requires `nasm` only if you want to modify the test program
  and regenerate its byte array; the firmware itself has no assembler
  dependency, the bytes are already baked in

## Known limitations / future work

- **DDR3 is not yet working** through the open-source toolchain -- the
  Kintex-7 `prjxray-db` is missing segment definitions for some I/O bank
  cascade primitives the DDR3 PHY needs (`IOB_COL_OBUF_CASCADE_Y1`,
  `IOB_COL_BANK_ACTIVE`). Two ways forward: patch/complete that database, or
  synthesize with a paid Vivado Standard/Enterprise license instead.
- **Button-controlled runtime overclock** (a `BUFGMUX`-based clock switch
  selecting between two PLL outputs) was prototyped and confirmed working
  for LED speed, but caused a spurious-interrupt regression that wasn't
  fully run to ground before being reverted. See
  `fpga/overclock-experiment/` for the actual code and a full writeup of
  what broke -- worth revisiting with more time, the core idea (glitchless
  clock switching via a physical button) works.
