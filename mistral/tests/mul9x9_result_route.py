#!/usr/bin/env python3
"""Check that packed lane-2 outputs start at the physical RESULT.37 port.

The issue reproducer routes all three multiplier outputs.  Its routed JSON
retains the first fabric wire for each DSP output net; the decompressed
bitstream lists the corresponding DSP RESULT-to-wire connection.  This test
uses those two host artifacts to catch an off-by-one lane-2 result slice.
"""
import argparse
import json
import re
from pathlib import Path


def node_key(node):
    """Normalize Mistral's zero-padded bitstream node spelling."""
    match = re.fullmatch(r"([A-Z0-9]+)\.(\d+)\.(\d+)\.(\d+)", node)
    if not match:
        return node
    return (match.group(1), *(int(value) for value in match.groups()[1:]))


def first_route_node(net):
    routing = net.get("attributes", {}).get("ROUTING", "")
    first = routing.split(";", 1)[0]
    assert first, net
    return node_key(first)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--routed", required=True, type=Path)
    parser.add_argument("--bitstream-text", required=True, type=Path)
    parser.add_argument("--cell", default="lane2")
    args = parser.parse_args()

    module = json.loads(args.routed.read_text())["modules"]["top"]
    cells = [
        cell for name, cell in module["cells"].items()
        if cell["type"] == "MISTRAL_MUL9X9" and args.cell in name
    ]
    assert len(cells) == 1, cells
    cell = cells[0]

    net_by_bit = {
        bit: net for net in module["netnames"].values()
        for bit in net.get("bits", []) if isinstance(bit, int)
    }
    result_routes = {}
    for line in args.bitstream_text.read_text().splitlines():
        match = re.match(r"r (DSP\.\d+\.\d+):RESULT\.(\d+) (\S+)$", line)
        if match:
            result_routes[node_key(match.group(3))] = int(match.group(2))

    observed = []
    for bit in cell["connections"]["Y"]:
        # The repro exposes the low 16 product bits.  Unconsumed high bits
        # have no routing attribute and are intentionally skipped here.
        routing = net_by_bit[bit].get("attributes", {}).get("ROUTING", "")
        if not routing.strip():
            continue
        destination = first_route_node(net_by_bit[bit])
        assert destination in result_routes, (bit, destination)
        observed.append(result_routes[destination])

    assert len(observed) == 16, observed
    assert observed == list(range(37, 53)), observed
    print("PASS lane2 RESULT", observed[0], observed[-1])


if __name__ == "__main__":
    main()
