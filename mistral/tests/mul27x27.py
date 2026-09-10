#!/usr/bin/env python3
"""Route a single 27x27 DSP primitive through one Cyclone V DSP site.

Accepts the real 430 design or widens the locked 060 multiplier for a host-side
cascade mode test. Placement, routing, timing, and control programming are the
assertions under test; arithmetic requires a separate kit probe.
"""

import argparse
import json
from pathlib import Path
import re
import subprocess


def make_fixture(path: Path, controls=None) -> dict:
    design = json.loads(path.read_text())
    cells = design["modules"]["top"]["cells"]
    names = [name for name, cell in cells.items() if cell["type"] in ("MISTRAL_MUL9X9", "MISTRAL_MUL27X27")]
    assert len(names) == 1, names
    mul = cells[names[0]]
    if mul["type"] == "MISTRAL_MUL9X9":
        mul["type"] = "MISTRAL_MUL27X27"
        mul["attributes"]["src"] = mul["attributes"].get("src", "") + "|dsp-mode-test:27x27"
        mul["parameters"].update(CASCADE_EN="1", CASCADE_1ST_EN="1", CHAIN_OUTPUT_EN="1")
        mul["port_directions"]["ACCUMULATE"] = "input"
        mul["connections"]["ACCUMULATE"] = [4]
        mul["connections"]["A"] = list(mul["connections"]["A"]) + ["0"] * 18
        mul["connections"]["B"] = list(mul["connections"]["B"]) + ["0"] * 18

    if controls is not None:
        for control in ("ACCUMULATE", "LOADCONST", "NEGATE", "SUB"):
            if controls == "omitted":
                mul["connections"].pop(control, None)
                mul["port_directions"].pop(control, None)
            else:
                mul["port_directions"][control] = "input"
                mul["connections"][control] = [controls]

    used = [
        bit
        for cell in cells.values()
        for bits in cell.get("connections", {}).values()
        for bit in bits
        if isinstance(bit, int)
    ]
    next_net = max(used) + 1
    missing_y_bits = 54 - len(mul["connections"]["Y"])
    assert missing_y_bits >= 0
    mul["connections"]["Y"] = list(mul["connections"]["Y"]) + list(range(next_net, next_net + missing_y_bits))
    return design


def run(command, log: Path):
    with log.open("w") as stream:
        subprocess.run(command, stdout=stream, stderr=subprocess.STDOUT, check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("nextpnr", "mistral-cv", "fixture", "qsf", "sdc", "output"):
        parser.add_argument("--" + name, required=True, type=Path)
    parser.add_argument("--controls", choices=("omitted", "0", "1"),
                        help="remove or tie all four arithmetic controls; preserve fixture controls by default")
    args = parser.parse_args()

    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    fixture = out / "synth.json"
    design = make_fixture(args.fixture, args.controls)
    fixture.write_text(json.dumps(design))
    mul = next(cell for cell in design["modules"]["top"]["cells"].values()
               if cell["type"] == "MISTRAL_MUL27X27")
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
    assert util["MISTRAL_MUL27X27"] == {"used": 1, "available": 112}, util
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
    sites = re.findall(r"^s (DSP\.\d+\.\d+):MODE M27X27$", bt, re.M)
    assert len(sites) == 1, sites
    site = sites[0]
    settings = dict(re.findall(r"^s " + re.escape(site) + r":(\S+) (\S+)$", bt, re.M))
    for setting in ("CASCADE_EN", "CASCADE_1ST_EN", "CHAIN_OUTPUT_EN"):
        expected = int(mul["parameters"].get(setting, "0"), 2)
        assert int(settings.get(setting, "0")) == expected, settings
    if args.controls is not None:
        for control, setting in (("ACCUMULATE", "ACC_INV"), ("LOADCONST", "PRELOAD_INV"),
                                 ("NEGATE", "DEC_INV"), ("SUB", "SUB_INV")):
            expected = "0" if args.controls == "1" else "1"
            assert settings.get(setting, "0") == expected, settings
            assert not re.search(r"^r \S+ " + re.escape(f"{site}:{control}") + r"$", bt, re.M), control
    else:
        assert f"{site}:ACCUMULATE" in bt
    assert (out / "top.rbf").read_bytes()
    print("PASS", sites[0])


if __name__ == "__main__":
    main()
