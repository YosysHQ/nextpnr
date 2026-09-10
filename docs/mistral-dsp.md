# Cyclone V DSP support

The Mistral architecture exposes three `MISTRAL_MUL9X9` BEL lanes, one
`MISTRAL_MUL18X18` BEL, and one `MISTRAL_MUL27X27` BEL per physical DSP block.
`5CSEBA6U23I7` has 112 blocks, so the device reports 336 placeable M9 lanes or
112 wide-mode BELs. The packer groups up to three independent 9x9 cells into
one physical site and requires matching signedness, register, preadder, and
cascade settings because those controls are shared by the block. A wide BEL
owns its site and cannot share it with another multiplier mode.

`mistral/dsp.cc` imports `CycloneV::dsp_get_pos()` and adds the mode-specific
BEL views at each site. In M9 mode, lane 0 connects `A[8:0]`/`B[8:0]` to
DATAIN groups 0/2 and `Y[17:0]` to RESULT[17:0]; lanes 1 and 2 use groups 6/8
and 7/9 with result
slices 18:35 and 37:54. The physical RESULT namespace has a one-bit hole at
36 between the second and third 9x9 products; using 36 as lane 2's base makes
the fabric read `(product << 1) | 1` on Cyclone V hardware. These are Mistral's
existing ports and routing nodes, as documented in its
`docs/cyclonev_details.rst`; no routing or configuration tables are added.
BEL and cell names match, so the existing placer legality, BEL buckets and
default pin mapping apply without cell renaming. M18 mode maps the two 9-bit
halves of A and B to groups 0/1 and 2/3, the optional 36-bit C addend to groups
6..9, and Y to RESULT[35:0]. M27 mode maps A to groups 0/6/7, B to 2/8/9, and
Y to the 54-bit RESULT port with its physical hole at bit 36.

`mistral/pack.cc` forms strict same-site M9 clusters after grouping compatible
profiles in deterministic cell-name order. The root is z=0 and children are
z=1 and z=2. M18 and M27 cells are placed directly at their one wide-mode BEL.
`mistral/arch.cc` rejects mixed modes and incompatible shared settings at a
tile, including control-net mismatches between packed M9 lanes.

`mistral/bitstream.cc` configures each physical site once. It selects
`M9X9`, `M18X18P36`, or `M27X27`, writes the shared signedness and DATA_INV
settings, and programs the input/output register muxes. M9 supports the
reverse-engineered preadder mapping (`PREADDER_EN`/`PREADDER_SUB`). M18
supports its 36-bit C addend and the accumulator/cascade controls
(`ACCUMULATE`, `SUB`, `NEGATE`, `LOADCONST`, `CASCADE_EN`,
`CASCADE_1ST_EN`, and `CHAIN_OUTPUT_EN`). The independent three-lane M9 mode
does not support accumulator or cascade controls. M27 has no addend or
preadder port in this backend, but accepts the shared accumulator/cascade
controls.

The DSP control inputs are materialised by `mistral/pack.cc` even when Yosys
omits them. This is required because the arithmetic controls default high when
left floating. Constant zero/one and inverter drivers are folded into the
retained pin state, then emitted as the DSP arithmetic inversion bits
(`ACC_INV`, `PRELOAD_INV`, `SUB_INV`, and `DEC_INV`) or the registered enable
force/inversion bits. Unused asynchronous clear selects the low-default fabric input without
inversion. Constant-high clear uses its inversion bit. The same state is part of the M9 shared-control
legality check, preventing lanes tied to opposite constants from sharing one
site.

Global clock-buffer outputs use the dedicated DSP TCLK input. A connected
fabric `CLK` or `ACLR` net uses the additional `CLK_FABRIC` or `ACLR_FABRIC`
BEL pin, mapped to `CLKIN.3` or `ACLR.2`, respectively. The selectors follow
the selected BEL pins in every DSP mode. Hard clock constants use the fabric
input and its inversion bit.

M18 C bits use DATAIN groups `{8, 9, 6, 7}` in increasing significance order.
Quartus routing confirms that the two 18-bit halves must be exchanged relative
to numerical group order. This implements the P36 addend without enabling
cascade-chain mode.

The existing packer's constant/inverter folding is enabled through
`mistral/pins.cc`. DSP data inputs float high and have per-bit inversion controls.
`mistral/bitstream.cc` selects the mode, writes `AX_SIGNED`/`AY_SIGNED` from
the Yosys parameters (default true), and writes all twelve `DATA_INV` groups.
Unused inputs are zeroed. Register muxes default to bypass, while explicitly
enabled input/output registers, preaddition, accumulator, and cascade fields
are programmed from the DSP cell. No FFs are packed into the DSP. Configuration
API calls are checked for success.

`mistral/delay.cc` classifies the multiplier data and control ports and models
registered input/output timing when the register muxes are enabled. It uses
the Cyclone V arcs in locked Yosys `techlibs/intel_alm/common/dsp_sim.v`: M9
A→Y 2818 ps and B→Y 3051 ps, M18 A→Y 3180 ps and B/C→Y 3982 ps, and M27 A→Y
3732 ps and B→Y 3928 ps. These fixed arcs retain the source model's speed-grade
limitations.
The synchronous clock Fmax does not constrain asynchronous HPS GP operands.

## Mistral library

This backend uses existing Mistral DSP configuration and routing ports. DSP
blocks span two tiles. Reverse lookup of DATAIN/RESULT on the upper edge of a
BEL span needs Mistral to mark the upper routing tile (`T_DSP2`) in
`add_cram_blocks()`. That mapping is in Ravenslofty/mistral PR
https://github.com/Ravenslofty/mistral/pull/24 (and the DeanoC fork). nextpnr
CI currently builds against an older Mistral revision that still compiles this
arch; host regressions that decode DSP ports at span boundaries should use a
Mistral tree that includes the upper-tile fix.

## Host regression

Regenerate `build/oss/060_dsp_mul/synth.json` with locked Yosys in a separate,
clean misteross checkout. The original tools must fail placement with no
`MISTRAL_MUL9X9` BEL. Build this nextpnr with the accompanying Mistral source:

```sh
cmake -S "$NEXTPNR_SOURCE" -B "$DSP_BUILD" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DARCH=mistral -DMISTRAL_ROOT="$MISTRAL_SOURCE" \
  -DBUILD_PYTHON=OFF -DBUILD_GUI=OFF -DBUILD_TESTS=OFF -DUSE_IPO=OFF
cmake --build "$DSP_BUILD" --target nextpnr-mistral -j8
python3 "$NEXTPNR_SOURCE/mistral/tests/mul9x9.py" \
  --nextpnr "$DSP_BUILD/nextpnr-mistral" --mistral-cv "$MISTRAL_CV" \
  --fixture "$MISTEROSS/build/oss/060_dsp_mul/synth.json" \
  --qsf "$MISTEROSS/boards/de10nano/pins.qsf" \
  --sdc "$MISTEROSS/boards/de10nano/clocks.sdc" --output "$DSP_RESULTS"
python3 "$NEXTPNR_SOURCE/mistral/tests/mul9x9_multilane.py" \
  --nextpnr "$DSP_BUILD/nextpnr-mistral" --mistral-cv "$MISTRAL_CV" \
  --fixture "$MISTEROSS/build/oss/060_dsp_mul/synth.json" \
  --qsf "$MISTEROSS/boards/de10nano/pins.qsf" \
  --sdc "$MISTEROSS/boards/de10nano/clocks.sdc" --output "$DSP_RESULTS/multilane"
```

The mode and control fixtures use the same command-line arguments:

```sh
python3 "$NEXTPNR_SOURCE/mistral/tests/mul18x18.py" --nextpnr "$DSP_BUILD/nextpnr-mistral" --mistral-cv "$MISTRAL_CV" --fixture "$MISTEROSS/build/oss/060_dsp_mul/synth.json" --qsf "$MISTEROSS/boards/de10nano/pins.qsf" --sdc "$MISTEROSS/boards/de10nano/clocks.sdc" --output "$DSP_RESULTS/mul18x18"
python3 "$NEXTPNR_SOURCE/mistral/tests/mul27x27.py" --nextpnr "$DSP_BUILD/nextpnr-mistral" --mistral-cv "$MISTRAL_CV" --fixture "$MISTEROSS/build/oss/060_dsp_mul/synth.json" --qsf "$MISTEROSS/boards/de10nano/pins.qsf" --sdc "$MISTEROSS/boards/de10nano/clocks.sdc" --output "$DSP_RESULTS/mul27x27"
python3 "$NEXTPNR_SOURCE/mistral/tests/mul9x9_preadder.py" --nextpnr "$DSP_BUILD/nextpnr-mistral" --mistral-cv "$MISTRAL_CV" --fixture "$MISTEROSS/build/oss/060_dsp_mul/synth.json" --qsf "$MISTEROSS/boards/de10nano/pins.qsf" --sdc "$MISTEROSS/boards/de10nano/clocks.sdc" --output "$DSP_RESULTS/mul9x9-preadder"
python3 "$NEXTPNR_SOURCE/mistral/tests/mul18x18_mac.py" --nextpnr "$DSP_BUILD/nextpnr-mistral" --mistral-cv "$MISTRAL_CV" --fixture "$MISTEROSS/build/oss/060_dsp_mul/synth.json" --qsf "$MISTEROSS/boards/de10nano/pins.qsf" --sdc "$MISTEROSS/boards/de10nano/clocks.sdc" --output "$DSP_RESULTS/mul18x18-mac"
python3 "$NEXTPNR_SOURCE/mistral/tests/mul18x18_registered.py" --nextpnr "$DSP_BUILD/nextpnr-mistral" --mistral-cv "$MISTRAL_CV" --fixture "$MISTEROSS/build/oss/060_dsp_mul/synth.json" --qsf "$MISTEROSS/boards/de10nano/pins.qsf" --sdc "$MISTEROSS/boards/de10nano/clocks.sdc" --output "$DSP_RESULTS/mul18x18-registered"
```

The control-routing variants use the real ladder fixtures without changing
the misteross checkout:

```sh
python3 "$NEXTPNR_SOURCE/mistral/tests/mul18x18_mac.py" --nextpnr "$DSP_BUILD/nextpnr-mistral" --mistral-cv "$MISTRAL_CV" --fixture "$MISTEROSS/build/oss/450_dsp_mac/synth.json" --qsf "$MISTEROSS/boards/de10nano/pins.qsf" --sdc "$MISTEROSS/boards/de10nano/clocks.sdc" --output "$DSP_RESULTS/mul18x18-mac-real"
python3 "$NEXTPNR_SOURCE/mistral/tests/mul18x18_registered.py" --nextpnr "$DSP_BUILD/nextpnr-mistral" --mistral-cv "$MISTRAL_CV" --fixture "$MISTEROSS/build/oss/460_dsp_reg/synth.json" --qsf "$MISTEROSS/boards/de10nano/pins.qsf" --sdc "$MISTEROSS/boards/de10nano/clocks.sdc" --output "$DSP_RESULTS/mul18x18-registered-real"
python3 "$NEXTPNR_SOURCE/mistral/tests/mul18x18_registered.py" --fabric-controls --nextpnr "$DSP_BUILD/nextpnr-mistral" --mistral-cv "$MISTRAL_CV" --fixture "$MISTEROSS/build/oss/460_dsp_reg/synth.json" --qsf "$MISTEROSS/boards/de10nano/pins.qsf" --sdc "$MISTEROSS/boards/de10nano/clocks.sdc" --output "$DSP_RESULTS/mul18x18-registered-fabric"
python3 "$NEXTPNR_SOURCE/mistral/tests/mul18x18_registered.py" --constant-controls --nextpnr "$DSP_BUILD/nextpnr-mistral" --mistral-cv "$MISTRAL_CV" --fixture "$MISTEROSS/build/oss/460_dsp_reg/synth.json" --qsf "$MISTEROSS/boards/de10nano/pins.qsf" --sdc "$MISTEROSS/boards/de10nano/clocks.sdc" --output "$DSP_RESULTS/mul18x18-registered-constant"
```

These fixtures check the mode/control fields in the decompressed bitstream,
one DSP used, no M10K or PLL use, a compressed nonempty RBF, and a passing
50 MHz clock constraint. They are host-only configuration checks; they do not
program a board or verify arithmetic.

Use `mistral-cv` built with the upper-tile fix. The single-lane regression routes
the original fixture plus explicit unsigned/constant-high and mixed-signed/input
inversion variants. The multi-lane regression clones the fixture into two- and
three-cell designs, checks one physical site with z lanes 0/1/2, and decodes
the compressed RBF DATA_INV masks. Both require one HPS GP, zero M10K and a
passing intended 50 MHz clock. This is host configuration evidence, not an
arithmetic hardware test.

The lane-2 result-route regression uses the exact three-lane reproducer from
`DeanoC/mistral-dsp-lane2-repro`. After synthesizing its `rtl/top.v` with the
locked Yosys and routing it, run:

```sh
python3 "$NEXTPNR_SOURCE/mistral/tests/mul9x9_result_route.py" \
  --routed "$REPRO/build/current/routed.json" \
  --bitstream-text "$REPRO/build/current/top.bt"
```

It fails on the old mapping with `RESULT.36..51` and passes with
`RESULT.37..52` for the exposed low 16 product bits.
