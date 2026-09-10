# DSP controls and C-addend regression evidence

These are diagnostic results on the configured DE10-Nano kit, 2026-09-07.
They cover changes following nextpnr `89f7f1935e746ac38b94a0b65e9121a0d8fcce6b`
in PR #30, built against Mistral `78ba2a580ae2523403d4f4f91891a6b11d7b6aba`.
The integration pins were not changed.

`host-results.json` records the exact synthesized fixture and compressed RBF
SHA-256, utilization, and clock report for each variant. The input JSONs came
from the parallel misteross ladder's 430, 450, and 460 experiments; the legacy
cascade fixture widens 060. This record identifies the generated JSON by hash,
without claiming an uncommitted ladder build came from a clean revision.

Run the host scripts using the commands in `docs/mistral-dsp.md`. For the M27
workaround-free cases, pass the 430 JSON to `mul27x27.py --controls omitted`
and `--controls 0`; `--controls 1` checks the opposite inversion settings.
For constant clocks/enables, pass the 460 JSON to `mul18x18_registered.py`
with `--constant-clock 0 --constant-enable 0`, then repeat with both set to 1.

The checked-in hardware logs are outputs of `mistral/tests/dsp_probe.sh`:

- M27, all arithmetic controls omitted: 10*12=120 and 255*255=65025.
- M18 MAC, cascade disabled: 10*12+5=125; additional addend vectors pass.
- M18 input/output registers, unused clear: 10*12=120, 255*255=65025;
  disabling ENA holds 120 while inputs change, and re-enabling produces 1.

To repeat, claim the designated kit with misteross `scripts/kit.py`, load the
corresponding newly generated RBF, then run `dsp_probe.sh` on that target via
SSH while retaining the lease. The probe only accesses HPS GP registers.
Use the session's `stop` and `release` commands afterwards; automatic development
reboot recovery was used here. No direct SSH reboot or programming was used.
The MAC, registered, and omitted-control M27 RBFs were individually tested;
fabric-control and hard-constant variants are host configuration/routing checks.

The original MAC mapping was independently checked against a Quartus 17.0.2
oracle for unsigned `Y = A * B + C`, with 18-bit A/B and 36-bit C/Y. Tracing
its routes back to input pads gives this physical map:

| Logical bits | DATAIN group |
| --- | --- |
| C[8:0] | 8 |
| C[17:9] | 9 |
| C[26:18] | 6 |
| C[35:27] | 7 |

The updated MAC host check fails on the prior binary at missing group 8 bit 0
for the 450 fixture, then passes after correcting the order. Hardware before
that correction returned 120 for 10*12+5; after it, the result is 125.
The unused fabric clear also needs its own polarity: unlike DSP arithmetic
inputs, it defaults low. Selecting ACLR.2 without inversion lets the registered
fixture run; inverting that unused input held the registers in reset.
