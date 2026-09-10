#!/usr/bin/env python3
"""Route one M9 lane with the documented Z preadder path enabled."""

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
    mul["parameters"].update(PREADDER_EN="1", PREADDER_SUB="1")
    mul["port_directions"]["Z"] = "input"
    mul["connections"]["Z"] = list(mul["connections"]["A"])
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
    assert util["MISTRAL_MUL9X9"] == {"used": 1, "available": 336}, util
    assert util["cyclonev_hps_interface_mpu_general_purpose"]["used"] == 1
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
    sites = re.findall(r"^s (DSP\.\d+\.\d+):MODE M9X9$", bt, re.M)
    assert len(sites) == 1, sites
    site = sites[0]
    settings = dict(re.findall(r"^s " + re.escape(site) + r":(\S+) (\S+)$", bt, re.M))
    assert settings["PREADDER_EN"] == "1", settings
    assert settings["PREADDER_SUB"] == "1", settings
    assert f"{site}.4:DATAIN.0" in bt
    assert (out / "top.rbf").read_bytes()
    print("PASS", site)


if __name__ == "__main__":
    main()
