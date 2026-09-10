#!/usr/bin/env python3
"""Route a single 18x18 DSP primitive through one Cyclone V DSP site.

The fixture starts with the locked 060 design so the board clock, HPS GP and
IO constraints are identical to the production flow.  The existing 9x9 cell
is widened only for this host-side backend test; the extra result bits are
left as sinks because the test is checking placement, routing and bitstream
mode selection rather than application behavior.
"""

import argparse
import json
from pathlib import Path
import re
import subprocess


def make_fixture(path: Path) -> dict:
    design = json.loads(path.read_text())
    cells = design["modules"]["top"]["cells"]
    names = [name for name, cell in cells.items() if cell["type"] == "MISTRAL_MUL9X9"]
    assert len(names) == 1, names
    mul = cells[names[0]]
    mul["type"] = "MISTRAL_MUL18X18"
    mul["attributes"]["src"] = mul["attributes"].get("src", "") + "|dsp-mode-test:18x18"
    # Reuse the low-bit nets for the high slices so the physical group
    # mapping is observable in the decompiled DATA_INV settings.
    low_a = mul["connections"]["A"][0]
    low_b = mul["connections"]["B"][0]
    mul["connections"]["A"] = list(mul["connections"]["A"]) + [low_a] * 9
    mul["connections"]["B"] = list(mul["connections"]["B"]) + [low_b] * 9

    used = [
        bit
        for cell in cells.values()
        for bits in cell.get("connections", {}).values()
        for bit in bits
        if isinstance(bit, int)
    ]
    next_net = max(used) + 1
    mul["connections"]["Y"] = list(mul["connections"]["Y"]) + list(range(next_net, next_net + 18))
    return design


def run(command, log: Path):
    with log.open("w") as stream:
        subprocess.run(command, stdout=stream, stderr=subprocess.STDOUT, check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("nextpnr", "mistral-cv", "fixture", "qsf", "sdc", "output"):
        parser.add_argument("--" + name, required=True, type=Path)
    args = parser.parse_args()

    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    fixture = out / "synth.json"
    fixture.write_text(json.dumps(make_fixture(args.fixture)))
    run(
        [
            str(args.nextpnr.resolve()),
            "--json",
            str(fixture),
            "--device",
            "5CSEBA6U23I7",
            "--qsf",
            str(args.qsf.resolve()),
            "--sdc",
            str(args.sdc.resolve()),
            "--freq",
            "50",
            "--compress-rbf",
            "--rbf",
            str(out / "top.rbf"),
            "--write",
            str(out / "routed.json"),
            "--report",
            str(out / "timing.json"),
            "--detailed-timing-report",
        ],
        out / "route.log",
    )

    report = json.loads((out / "timing.json").read_text())
    util = report["utilization"]
    assert util["MISTRAL_MUL18X18"] == {"used": 1, "available": 112}, util
    assert util["cyclonev_hps_interface_mpu_general_purpose"]["used"] == 1
    assert util["MISTRAL_MUL9X9"]["used"] == 0
    assert util["MISTRAL_M10K"]["used"] == 0
    assert util["altera_pll"]["used"] == 0
    clock = report["fmax"]["product.FPGA_CLK1_50"]
    assert clock["constraint"] == 50 and clock["achieved"] >= 50, clock

    run(
        [
            str(args.mistral_cv.resolve()),
            "decomp",
            "5CSEBA6U23I7",
            str(out / "top.rbf"),
            str(out / "top.bt"),
        ],
        out / "decomp.log",
    )
    bt = (out / "top.bt").read_text()
    sites = re.findall(r"^s (DSP\.\d+\.\d+):MODE M18X18P36$", bt, re.M)
    assert len(sites) == 1, sites
    routes = bt
    assert re.search(r"DSP\.\d+\.\d+\.1:DATAIN\.", routes)
    assert re.search(r"DSP\.\d+\.\d+\.3:DATAIN\.", routes)
    assert not re.search(r"DSP\.\d+\.\d+\.6:DATAIN\.", routes)
    assert not re.search(r"DSP\.\d+\.\d+\.8:DATAIN\.", routes)
    assert (out / "top.rbf").read_bytes()
    print("PASS", sites[0])


if __name__ == "__main__":
    main()
