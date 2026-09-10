#!/usr/bin/env python3
"""Route multiple 9x9 multipliers and require one physical DSP site.

The input fixture is the locked 060 DSP design.  Its multiplier is cloned
with shared inputs and independent (unconsumed) outputs so the test isolates
DSP packing and placement.  The route must use distinct logical BEL lanes at
one physical DSP site.
"""
import argparse
import copy
import json
from pathlib import Path
import subprocess


def run(command, log):
    with log.open("w") as stream:
        subprocess.run(command, stdout=stream, stderr=subprocess.STDOUT, check=True)


def duplicate_fixture(path, count):
    design = json.loads(path.read_text())
    module = design["modules"]["top"]
    names = [name for name, cell in module["cells"].items() if cell["type"] == "MISTRAL_MUL9X9"]
    assert len(names) == 1, names
    original_name = names[0]
    original = module["cells"][original_name]
    assert count in (2, 3)

    integer_bits = [
        bit
        for cell in module["cells"].values()
        for bits in cell.get("connections", {}).values()
        for bit in bits
        if isinstance(bit, int)
    ]
    next_net = max(integer_bits) + 1
    for lane in range(1, count):
        clone = copy.deepcopy(original)
        clone["attributes"]["src"] = clone["attributes"].get("src", "") + f"|multilane:{lane}"
        clone["connections"]["Y"] = list(range(next_net, next_net + len(clone["connections"]["Y"])))
        next_net += len(clone["connections"]["Y"])
        # Make each lane's DATA_INV programming distinguishable while keeping
        # the shared signedness and M9X9 mode compatible.
        if lane == 1:
            clone["connections"]["A"][0] = "0"
            clone["connections"]["B"][0] = "0"
        else:
            clone["connections"]["A"][1] = "0"
            clone["connections"]["B"][1] = "0"
        module["cells"][f"{original_name}_lane{lane}"] = clone
    return design


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--nextpnr", required=True, type=Path)
    parser.add_argument("--mistral-cv", required=True, type=Path)
    parser.add_argument("--fixture", required=True, type=Path)
    parser.add_argument("--qsf", required=True, type=Path)
    parser.add_argument("--sdc", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()

    for count in (2, 3):
        out = args.output / f"{count}x9"
        out.mkdir(parents=True, exist_ok=True)
        (out / "synth.json").write_text(json.dumps(duplicate_fixture(args.fixture, count)))
        run(
            [
                str(args.nextpnr.resolve()),
                "--json",
                str((out / "synth.json").resolve()),
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
                str((out / "top.rbf").resolve()),
                "--write",
                str((out / "routed.json").resolve()),
                "--report",
                str((out / "timing.json").resolve()),
                "--detailed-timing-report",
            ],
            out / "route.log",
        )
        report = json.loads((out / "timing.json").read_text())
        util = report["utilization"]["MISTRAL_MUL9X9"]
        assert util["used"] == count, util
        assert util["available"] == 336, util
        assert report["utilization"]["cyclonev_hps_interface_mpu_general_purpose"]["used"] == 1
        assert report["utilization"]["MISTRAL_M10K"]["used"] == 0
        assert report["utilization"]["altera_pll"]["used"] == 0
        assert report["fmax"]["product.FPGA_CLK1_50"]["achieved"] >= 50

        routed = json.loads((out / "routed.json").read_text())["modules"]["top"]["cells"]
        bels = [
            cell["attributes"]["NEXTPNR_BEL"]
            for cell in routed.values()
            if cell["type"] == "MISTRAL_MUL9X9"
        ]
        assert len(bels) == count, bels
        sites = {".".join(bel.split(".")[:3]) for bel in bels}
        assert len(sites) == 1, (count, bels)
        assert {int(bel.rsplit(".", 1)[1]) for bel in bels} == set(range(count)), bels

        run(
            [
                str(args.mistral_cv.resolve()),
                "decomp",
                "5CSEBA6U23I7",
                str((out / "top.rbf").resolve()),
                str((out / "top.bt").resolve()),
            ],
            out / "decomp.log",
        )
        settings = {}
        x, y = next(iter(sites)).split(".")[1:]
        site = f"DSP.{int(x):03d}.{int(y):03d}"
        for line in (out / "top.bt").read_text().splitlines():
            prefix = f"s {site}:"
            if line.startswith(prefix):
                key, value = line[len(prefix) :].split(" ", 1)
                settings[key] = value
        assert settings["MODE"] == "M9X9", settings
        assert settings["AX_SIGNED"] == "1" and settings["AY_SIGNED"] == "1", settings
        expected_masks = {0: "100", 2: "100", 6: "101", 8: "101"}
        if count == 3:
            expected_masks.update({7: "102", 9: "102"})
        for group in range(12):
            assert settings[f"DATA_INV.{group}"] == expected_masks.get(group, "1ff"), (count, group, settings)
        assert (out / "top.rbf").read_bytes()
        print(count, "PASS", sites.pop(), sorted(bels), flush=True)


if __name__ == "__main__":
    main()
