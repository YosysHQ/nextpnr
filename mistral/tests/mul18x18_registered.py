#!/usr/bin/env python3
"""Route a registered M18 multiplier with an inactive asynchronous clear."""

import argparse
import json
from pathlib import Path
import re
import subprocess


def make_fixture(path: Path) -> dict:
    design = json.loads(path.read_text())
    cells = design["modules"]["top"]["cells"]
    names = [name for name, cell in cells.items() if cell["type"] in ("MISTRAL_MUL9X9", "MISTRAL_MUL18X18")]
    assert len(names) == 1, names
    mul = cells[names[0]]
    if mul["type"] == "MISTRAL_MUL9X9":
        mul["type"] = "MISTRAL_MUL18X18"
        mul["parameters"].update(INREG_CTRL_AX="reg", INREG_CTRL_AY="reg", OREG_CTRL="reg")
        mul["connections"]["A"] = list(mul["connections"]["A"]) + ["0"] * 9
        mul["connections"]["B"] = list(mul["connections"]["B"]) + ["0"] * 9
        mul["port_directions"]["CLK"] = "input"
        mul["connections"]["CLK"] = [4]
        mul["port_directions"]["ENA"] = "input"
        mul["connections"]["ENA"] = [4]

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
    parser.add_argument(
        "--fabric-controls",
        action="store_true",
        help="drive CLK and ACLR from an HPS fabric net instead of the dedicated clock input",
    )
    parser.add_argument(
        "--constant-controls",
        action="store_true",
        help="tie ENA, ACLR, and arithmetic controls high to exercise hard constants",
    )
    parser.add_argument("--constant-clock", choices=("0", "1"))
    parser.add_argument("--constant-enable", choices=("0", "1"))
    args = parser.parse_args()

    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    fixture = out / "synth.json"
    design = make_fixture(args.fixture)
    expected_ena_inv = "0"
    if args.constant_controls:
        cells = design["modules"]["top"]["cells"]
        mul = next(cell for cell in cells.values() if cell["type"] == "MISTRAL_MUL18X18")
        for control in ("ENA", "ACLR", "ACCUMULATE", "SUB", "NEGATE", "LOADCONST"):
            mul["port_directions"][control] = "input"
            mul["connections"][control] = ["1"]
    else:
        cells = design["modules"]["top"]["cells"]
        mul = next(cell for cell in cells.values() if cell["type"] == "MISTRAL_MUL18X18")
        ena_bits = mul.get("connections", {}).get("ENA", [])
        if any(cell["type"] == "MISTRAL_NOT" and cell.get("connections", {}).get("Q") == ena_bits for cell in cells.values()):
            expected_ena_inv = "1"
    if args.fabric_controls:
        cells = design["modules"]["top"]["cells"]
        mul = next(cell for cell in cells.values() if cell["type"] == "MISTRAL_MUL18X18")
        hps = next(cell for cell in cells.values() if cell["type"] == "cyclonev_hps_interface_mpu_general_purpose")
        fabric_net = next(bit for bit in reversed(hps["connections"]["gp_out"]) if isinstance(bit, int))
        mul["port_directions"]["CLK"] = "input"
        mul["connections"]["CLK"] = [fabric_net]
        mul["port_directions"]["ACLR"] = "input"
        mul["connections"]["ACLR"] = [fabric_net]
    if args.constant_clock is not None:
        mul["connections"]["CLK"] = [args.constant_clock]
    if args.constant_enable is not None:
        mul["port_directions"]["ENA"] = "input"
        mul["connections"]["ENA"] = [args.constant_enable]
        expected_ena_inv = "1" if args.constant_enable == "0" else "0"
    fixture.write_text(json.dumps(design))
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
    assert settings["INREG_CTRL_AX"] == "REG", settings
    assert settings["INREG_CTRL_AY"] == "REG", settings
    assert settings["OREG_CTRL"] == "REG", settings
    # The fabric enable is routed, so the register is not forced on.
    assert settings.get("ENABLE0_FORCE", "0") == ("1" if args.constant_enable == "1" or (args.constant_enable is None and args.constant_controls) else "0"), settings
    assert settings.get("ENABLE0_INV", "0") == expected_ena_inv, settings
    # ACLR is absent from the Yosys cell and must still be configured low.
    expected_aclr_inv = "1" if args.constant_controls and not args.fabric_controls else "0"
    assert settings.get("ACLR0_INV", "0") == expected_aclr_inv, settings
    assert settings["ACLR0_SEL"] == "2", settings
    assert settings["ACLR1_SEL"] == "3", settings
    assert settings["CLK1_SEL"] == "4", settings
    assert settings["CLK2_SEL"] == "5", settings
    for setting in ("ACC_INV", "PRELOAD_INV", "SUB_INV", "DEC_INV"):
        assert settings.get(setting, "0") == ("0" if args.constant_controls else "1"), settings
    if args.constant_clock is not None:
        assert settings["CLK0_SEL"] == "3", settings
        assert settings.get("CLK0_INV", "0") == args.constant_clock, settings
        assert f"{site}:CLKIN." not in bt
    elif args.fabric_controls:
        assert settings["CLK0_SEL"] == "3", settings
        assert f"{site}:CLKIN.3" in bt
        assert f"{site}:ACLR.2" in bt
    else:
        assert f"{site}:CLKIN.0" in bt
    if args.constant_controls or args.constant_enable is not None:
        assert f"{site}:ENABLE.0" not in bt
    else:
        assert f"{site}:ENABLE.0" in bt
    assert (out / "top.rbf").read_bytes()
    print("PASS", site)


if __name__ == "__main__":
    main()
