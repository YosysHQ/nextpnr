#!/usr/bin/env python3
"""Route an M18X18P36 multiply-add with zeroed DSP controls."""

import argparse
import json
from pathlib import Path
import re
import subprocess


def make_fixture(path: Path) -> dict:
    design = json.loads(path.read_text())
    module = design["modules"]["top"]
    cells = module["cells"]
    names = [name for name, cell in cells.items() if cell["type"] in ("MISTRAL_MUL9X9", "MISTRAL_MUL18X18")]
    assert len(names) == 1, names
    mul = cells[names[0]]
    if mul["type"] == "MISTRAL_MUL9X9":
        mul["type"] = "MISTRAL_MUL18X18"
        mul["connections"]["A"] = list(mul["connections"]["A"]) + ["0"] * 9
        mul["connections"]["B"] = list(mul["connections"]["B"]) + ["0"] * 9
        mul["port_directions"]["C"] = "input"
        mul["connections"]["C"] = list(mul["connections"]["A"][:9]) * 4
        # Exercise hard constant handling on every arithmetic control.
        for control in ("ACCUMULATE", "NEGATE", "LOADCONST", "SUB"):
            mul["port_directions"][control] = "input"
            mul["connections"][control] = ["0"]
    for parameter in ("CASCADE_EN", "CASCADE_1ST_EN", "CHAIN_OUTPUT_EN"):
        mul["parameters"].pop(parameter, None)

    if len(mul["connections"]["Y"]) == 18:
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
    design = make_fixture(args.fixture)
    fixture.write_text(json.dumps(design))
    mul = next(cell for cell in design["modules"]["top"]["cells"].values()
               if cell["type"] == "MISTRAL_MUL18X18")
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
    assert report["fmax"]["product.FPGA_CLK1_50"]["achieved"] >= 50

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
    site = sites[0]
    settings = dict(re.findall(r"^s " + re.escape(site) + r":(\S+) (\S+)$", bt, re.M))
    for setting in ("CASCADE_EN", "CASCADE_1ST_EN", "CHAIN_OUTPUT_EN"):
        assert settings.get(setting, "0") == "0", settings
    # Quartus uses the invert bits to make the unused control inputs low. The
    # same settings are required for a constant-zero JSON connection.
    for setting in ("ACC_INV", "PRELOAD_INV", "SUB_INV", "DEC_INV"):
        assert settings[setting] == "1", settings
    assert settings["ACLR0_SEL"] == "2", settings
    assert settings["ACLR1_SEL"] == "3", settings
    # Quartus's A*B+C oracle routes C[17:0] to groups 8/9 and C[35:18]
    # to groups 6/7. The 450 fixture has 16 variable low bits: routing them
    # to 6/7 instead silently adds C<<18, invisible on its low-16-bit GPI.
    c_bits = mul["connections"]["C"]
    assert len(c_bits) == 36, c_bits
    for slice_index, group in enumerate((8, 9, 6, 7)):
        expected_inv = 0
        for bit, net in enumerate(c_bits[9 * slice_index:9 * (slice_index + 1)]):
            endpoint = f"{site}.{group}:DATAIN.{bit}"
            routed = re.search(r"^r \S+ " + re.escape(endpoint) + r"$", bt, re.M)
            if isinstance(net, int):
                assert routed, endpoint
            else:
                assert net in ("0", "1"), net
                assert not routed, endpoint
                expected_inv |= (net == "0") << bit
        actual_inv = int(settings.get(f"DATA_INV.{group}", "0"), 16)
        assert actual_inv == expected_inv, (group, actual_inv, expected_inv)
    assert (out / "top.rbf").read_bytes()
    print("PASS", site)


if __name__ == "__main__":
    main()
