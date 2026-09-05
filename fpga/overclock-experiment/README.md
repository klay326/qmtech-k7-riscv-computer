# Overclock experiment (known broken -- kept for reference)

A button-controlled runtime clock switch for the LiteX SoC: hold SW3 to
switch the CPU from 100MHz to 150MHz live, via a glitchless `BUFGMUX`
selecting between two PLL outputs. Confirmed working for its core idea (the
LED chase pattern visibly sped up ~1.5x while the button was held), but never
got to a fully working state -- **do not build this expecting it to work
cleanly**.

## What's here

- `qmtech_kintex7_devboard.py` -- modified copy of the LiteX target script
  (starts from the stable version in `../../litex-soc/`) adding:
  - A `BUFGMUX`-based clock switch in the `_CRG`, selecting between
    `sys_clk_freq` and `1.5 * sys_clk_freq` based on the SW3 button
  - A `GPIOIn` peripheral (`overclock_btn`) so firmware can read SW3's state
  - A `MultiReg` synchronizer on the BUFGMUX select line (added after
    hitting the bug below, per Xilinx's own guidance to always synchronize a
    BUFGMUX select signal)
- `main_with_overclock.c` -- firmware variant that polls `overclock_btn` and
  reprograms the UART's dynamic baudrate tuning word CSR
  (`uart_phy_tuning_word`) to compensate, so the serial console doesn't
  garble at the higher clock speed. Requires building the SoC with
  `--uart-with-dynamic-baudrate`.

Build with (from a location where `../openXC7.mk`-style shared rules and a
LiteX venv are set up -- see main README):
```bash
python3 qmtech_kintex7_devboard.py --toolchain=openxc7 \
    --integrated-main-ram-size=0x8000 --uart-with-dynamic-baudrate --build
```

## The bug

Right after adding the `BUFGMUX` switch, every `reboot` triggered a flood of
`*** disabled spurious irq 3 ***` from the BIOS/firmware's interrupt-safety
code -- despite the SoC's own IRQ map only defining 2 real sources (UART=0,
timer0=1). Theory: the BUFGMUX select signal (SW3) was wired directly from a
raw async board input with no synchronization, and switching a clock mux
right as the PLL is still locking at reset is a known glitch source.

Added a `MultiReg` synchronizer on the select signal (matching Xilinx's own
guidance for `BUFGMUX` select inputs) -- this is the version committed here.
It was never fully verified: on the next test, the board stopped producing
*any* readable console output at all (worse than the IRQ flood), and rather
than keep debugging blind late at night, the whole change was reverted back
to the stable SoC (see `../../litex-soc/`) to get back to a known-good state.

**If you pick this back up:** the synchronizer fix may have only partially
addressed the real issue, or introduced a new one. Worth instrumenting more
carefully -- e.g. scoping the actual `cd_sys` clock signal during a `reboot`
to see what's really happening electrically during the PLL lock / BUFGMUX
switch window, rather than debugging from BIOS console symptoms alone.
