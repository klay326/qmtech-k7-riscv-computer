# LiteX SoC definition

The actual hardware description for the RISC-V computer: CPU choice
(VexRiscv, RV32IM), memory map, and peripherals (UART, LEDs, timer), which
LiteX turns into synthesizable Verilog + a matching set of C headers for the
firmware in `../firmware/led-chase/`.

- `qmtech_kintex7_devboard.py` -- the target script (from
  [litex-boards](https://github.com/litex-hub/litex-boards)), **unmodified**
  from upstream for the stable build used by `../firmware/led-chase/`.
  Included here so the whole project is visible in one place, not because we
  wrote it -- credit to the litex-boards maintainers listed in its header.
- `qmtech_kintex7_devboard_platform.py` -- the matching pin-mapping/platform
  file (also unmodified upstream), which is where the JP5 UART pins, LED
  pins, and switch pins for this board are actually defined.

For the modified version with a button-controlled runtime overclock (which
we built and got partially working, but never fully debugged), see
`../fpga/overclock-experiment/`.

## Building

Requires the [LiteX](https://github.com/enjoy-digital/litex) ecosystem
(`litex_setup.py --init --install --gcc=riscv`, ideally in a venv -- Arch and
other PEP 668 distros block installing outside one) and the same
`openxc7` toolchain used for the plain FPGA demos.

```bash
python3 qmtech_kintex7_devboard.py --toolchain=openxc7 \
    --integrated-main-ram-size=0x8000 --build
```

`--integrated-main-ram-size` uses internal block RAM instead of DDR3 -- see
the main README's "Known limitations" section for why.

This produces the bitstream (`build/qmtech_kintex7_devboard/gateware/*.bit`,
flash the same way as the plain FPGA demos) and the generated C headers that
`../firmware/led-chase/` builds against.
