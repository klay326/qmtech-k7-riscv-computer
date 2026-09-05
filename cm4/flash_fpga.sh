#!/bin/bash
# Run this ON the CM4 (not the dev machine) to reflash the FPGA over JTAG.
#
# CONFIRMED BLOCKED as of writing, with the current JTAG cable: this
# project's cable (a Xilinx-VID clone) only works with Vivado Lab Edition's
# driver -- openFPGALoader and xc3sprog both fail identically against its
# low-level JTAG protocol (see the main README). Vivado Lab Edition is
# x86_64-only with no ARM64 build and no plans for one (confirmed via AMD's
# own support forum), so it cannot run on the CM4 at all, emulation
# included (would be far too slow even if it worked).
#
# This script will therefore hit the same JTAG failure as the dev machine
# did before Vivado Lab Edition was installed there. It's kept here for
# when either of these becomes true:
#   1. A different, plain FTDI-based JTAG cable replaces the current one
#      (openFPGALoader has solid, cross-architecture support for those --
#      this would make self-flashing from the CM4 actually work), or
#   2. openFPGALoader/xc3sprog gain proper support for this specific cable
#      upstream.
#
# Until then, flashing stays a dev-machine-only operation (see the main
# README's Vivado Lab Edition flash.tcl workflow).
#
# Requires: openfpgaloader installed (apt install openfpgaloader on
# Raspberry Pi OS) and the JTAG cable plugged into the CM4's own USB port.
set -euo pipefail

BITSTREAM="${1:?Usage: $0 <path-to-bitstream.bit>}"

if [ ! -f "$BITSTREAM" ]; then
    echo "Bitstream not found: $BITSTREAM" >&2
    exit 1
fi

echo "Flashing $BITSTREAM via JTAG..."
openFPGALoader --board qmtechKintex7 --bitstream "$BITSTREAM"
echo "Done."
