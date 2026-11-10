"""Delays for every speed grade from a timing directory (`--timing`), in
place of X-Ray's one set. It is laid out as X-Ray's database is, one
directory per grade, named by the part's suffix (`1`, `2L`):

- `tile_type_<type>.json`: `pips`, each `src_wire`, `dst_wire`,
  `src_to_dst.delay` and `.in_cap` (a pip both ways is two), and `wires`,
  each `delay` and `res` -- a wire's delay is new to X-Ray's layout: a
  node's delay and res are its wires' summed.
- `timings/<site type>.sdf`: each cell type on a bel (`INSTANCE <site
  type>/<bel>`), its `IOPATH`s and `SETUPHOLD`s; and the site itself
  (`INSTANCE <site type>`): the way into a bel pin (`PORT`), out of one
  (`DEVICE`), and from one to another inside the site (`INTERCONNECT`).

Delays are in ns, as X-Ray writes them: in JSON `[fast min, fast max, slow
min, slow max]`, in SDF `(fast min::fast max)(slow min::slow max)` and a
check's one value `(fast min::slow max)`. A delay is a sum of terms along a
path: a pip's, its destination node's, and the ways into and out of bel
pins; and a node's load, as X-Ray's RC: its res (ohms) / 2 times the in_cap
(femtofarads) of the pips bound out of it.
"""
import json
import os
import re
from collections import defaultdict

import parse_sdf
from himbaechel_dbgen.chip import ClockEdge, TimingValue

# The cell types the packer makes, each timed as these cell types on these
# bels are: the first that has an arc gives it.
CELLS = {
    "SLICE_LUTX": [("LUT6", "SLICEL/A6LUT"), ("RAMD64E", "SLICEM/A6LUT"), ("SRLC32E", "SLICEM/A6LUT")],
    "SLICE_FFX": [("FDRE", "SLICEL/AFF")],
    "CARRY4": [("CARRY4", "SLICEL/CARRY4")],
    "SELMUX2_1": [("MUXF7", "SLICEL/F7AMUX")],
}
# Block RAM, a variant per write and read mode (true or simple dual port).
BRAMS = {"RAMB18E1_RAMB18E1": ("RAMB18E1", "RAMB18E1/RAMB18E1"), "RAMB36E1_RAMB36E1": ("RAMB36E1", "RAMB36E1/RAMB36E1")}

EDGES = {"posedge": ClockEdge.RISING, "negedge": ClockEdge.FALLING}


def value(quad):
    """A TimingValue of exactly these four (its constructor takes 0 as unset)."""
    v = TimingValue()
    v.fast_min, v.fast_max, v.slow_min, v.slow_max = quad
    return v


def ps(*ns):
    return [round(v * 1000) for v in ns]


def corners(entry):
    """An SDF delay's fast and slow (X-Ray's rising and falling) as a quad."""
    return ps(entry.rising.minv, entry.rising.maxv, entry.falling.minv, entry.falling.maxv)


def span(check):
    return ps(check.minv, check.maxv, check.minv, check.maxv)


class TimingDir:
    def __init__(self, path, timing):
        self.timing = timing
        self.grades = sorted(os.listdir(path))
        self.delays = defaultdict(dict)  # a way into, out of or within a site -> {grade: quad}
        self.cells = defaultdict(dict)  # (cell type, "<site type>/<bel>") -> {grade: SDF entries}
        self.pips = defaultdict(lambda: defaultdict(dict))  # tile type -> (src, dst) -> {grade: quad}
        self.wires = defaultdict(lambda: defaultdict(dict))  # tile type -> wire -> {grade: quad}
        self.in_cap = defaultdict(lambda: defaultdict(dict))  # tile type -> (src, dst) -> {grade: fF}
        self.res = defaultdict(lambda: defaultdict(dict))  # tile type -> wire -> {grade: ohms}
        for g in self.grades:
            for name in os.listdir(os.path.join(path, g)):
                m = re.fullmatch(r"tile_type_(.*)\.json", name)
                if not m:
                    continue
                with open(os.path.join(path, g, name)) as f:
                    tt = json.load(f)
                for pip in tt["pips"].values():
                    key = (pip["src_wire"], pip["dst_wire"])
                    self.pips[m.group(1)][key][g] = ps(*map(float, pip["src_to_dst"]["delay"]))
                    self.in_cap[m.group(1)][key][g] = float(pip["src_to_dst"].get("in_cap", 0))
                for wire, data in tt["wires"].items():
                    self.wires[m.group(1)][wire][g] = ps(*map(float, data["delay"]))
                    self.res[m.group(1)][wire][g] = float(data.get("res", 0))
            timings = os.path.join(path, g, "timings")
            for name in os.listdir(timings):
                for (cell_type, inst), cell in parse_sdf.parse_sdf_file(os.path.join(timings, name)).cells.items():
                    if "/" in inst:
                        self.cells[(cell_type, inst)][g] = cell.entries
                        continue
                    for e in cell.entries:
                        self.delays[f"{'in' if e.kind == 'PORT' else 'out'}:{inst}/{e.pin}"][g] = corners(e)
                    for (src, dst), e in cell.interconnect.items():
                        self.delays[f"inside:{inst}/{src}>{inst}/{dst}"][g] = corners(e)
        timing.set_speed_grades(self.grades)
        self.named = {}  # delays by grade -> pip class name
        self.nodes = set()

    def timed(self, delays):
        """These delays, where every grade has one."""
        return delays if delays and len(delays) == len(self.grades) else None

    def pip_class(self, delays, in_cap=None):
        """The pip class of these delays and in_caps by grade, made the
        first time."""
        in_cap = in_cap or {}
        key = tuple((tuple(delays[g]), in_cap.get(g, 0)) for g in self.grades)
        if key not in self.named:
            self.named[key] = name = f"t{len(self.named)}"
            for g in self.grades:
                # As X-Ray's: femtofarads the generator scales by 1000.
                self.timing.set_pip_class(g, name, value(delays[g]), in_cap=TimingValue(round(in_cap.get(g, 0) * 1000)))
        return self.named[key]

    def zero(self):
        return self.pip_class({g: [0, 0, 0, 0] for g in self.grades})

    def tile_pip(self, tile_type, src, dst):
        """A tile pip's class, or None where no grade times it."""
        delays = self.timed(self.pips[tile_type].get((src, dst)))
        return self.pip_class(delays, self.in_cap[tile_type][(src, dst)]) if delays else None

    def node_class(self, wires):
        """The node class of (tile type, wire)s: their delays and res summed."""
        timed = [(tt, w) for tt, w in wires if self.timed(self.wires[tt].get(w))]
        if not timed:
            return ""
        total = {g: [sum(self.wires[tt][w][g][i] for tt, w in timed) for i in range(4)] for g in self.grades}
        res = {g: round(sum(self.res[tt][w].get(g, 0) for tt, w in timed)) for g in self.grades}
        name = "n" + "_".join(str(v) for g in self.grades for v in total[g] + [res[g]])
        if name not in self.nodes:
            self.nodes.add(name)
            for g in self.grades:
                # As X-Ray's: ohms the generator takes as they are.
                self.timing.set_node_class(g, name, value(total[g]), res=TimingValue(res[g]))
        return name

    def site(self, sv):
        return Site(self, sv)

    def arcs(self, cell, entries, clocks=lambda pin, clock: clock):
        """Add these SDF entries' arcs on the rising edge to `cell`, each
        once; `clocks` names the clock a pin's check or output is by."""
        seen = set()
        # A pin the checks are clocked by: X-Ray writes its clock-to-out as
        # IOPATH CLK O6, without the edge.
        clock_pins = {e.clock[1] if isinstance(e.clock, list) else e.clock for e in entries if isinstance(e, parse_sdf.SetupHoldCheck)}
        for e in entries:
            if isinstance(e, parse_sdf.IOPath) and (isinstance(e.from_pin, list) or e.from_pin in clock_pins):
                edge, clock = e.from_pin if isinstance(e.from_pin, list) else ("posedge", e.from_pin)
                if edge == "posedge" and ("out", e.to_pin) not in seen:
                    seen.add(("out", e.to_pin))
                    cell.add_clock_out(clocks(e.to_pin, clock), e.to_pin, EDGES[edge], value(corners(e)))
            elif isinstance(e, parse_sdf.IOPath) and ("comb", e.from_pin, e.to_pin) not in seen:
                seen.add(("comb", e.from_pin, e.to_pin))
                cell.add_comb_arc(e.from_pin, e.to_pin, value(corners(e)))
            elif isinstance(e, parse_sdf.SetupHoldCheck):
                # SETUPHOLD names the edge; X-Ray's SETUP and HOLD, merged, the clock alone
                edge, clock = e.clock if isinstance(e.clock, list) else ("posedge", e.clock)
                if edge == "posedge" and ("check", e.pin) not in seen:
                    seen.add(("check", e.pin))
                    cell.add_setup_hold(clocks(e.pin, clock), e.pin, EDGES[edge], value(span(e.setup)), value(span(e.hold)))

    def cell_variants(self, fallback={}):
        """Every grade's cell variants; an arc the directory lacks takes
        `fallback`'s for its variant (X-Ray's SDF entries)."""
        for g in self.grades:
            for variant, sources in CELLS.items():
                cell = self.timing.add_cell_variant(g, variant)
                entries = [e for source in sources for e in self.cells.get(source, {}).get(g, [])]
                self.arcs(cell, entries + fallback.get(variant, []))
            for variant, source in BRAMS.items():
                # The output registers' clocks are not the uarch's; a
                # RAMB36's lower half is checked as its upper is.
                entries = [e for e in self.cells.get(source, {}).get(g, []) if "REGCLK" not in str(e.from_pin if isinstance(e, parse_sdf.IOPath) else e.clock)]
                if variant.startswith("RAMB36"):
                    entries += [lower(e) for e in entries if upper(e)]
                for wsdp in (False, True):
                    for rsdp in (False, True):
                        cell = self.timing.add_cell_variant(g, f"{variant}_{'WSDP' if wsdp else 'WTDP'}_{'RSDP' if rsdp else 'RTDP'}")
                        self.arcs(cell, entries, lambda pin, clock, wsdp=wsdp, rsdp=rsdp: dual_port(pin, clock, wsdp, rsdp))


def dual_port(pin, clock, wsdp, rsdp):
    """In simple dual port, port A's data is written by port B's clock, and
    port B's is read by port A's."""
    if wsdp and pin.startswith(("DIADI", "DIPADIP")):
        return clock.replace("CLKARDCLK", "CLKBWRCLK")
    if rsdp and pin.startswith(("DOBDO", "DOPBDOP")):
        return clock.replace("CLKBWRCLK", "CLKARDCLK")
    return clock


def upper(e):
    """A RAMB36 check on an upper-half pin (`WEAU0`) whose lower half
    (`WEAL0`) the SDF leaves out."""
    return isinstance(e, parse_sdf.SetupHoldCheck) and re.fullmatch(r"[A-Z]+U\d*", e.pin) and not e.pin.startswith("ADDR")


def lower(e):
    return parse_sdf.SetupHoldCheck(re.sub(r"U(\d*)$", r"L\1", e.pin), e.clock, e.setup, e.hold)


class Site:
    """The delays of one site variant's pips. A bel pin's way in (`in:`) is
    on the pip into the site wire it is on, and its way out (`out:`) on the
    pip out of it; the way from a site pin is the pin's entrance's delay and
    the pip after it the rest. Where one site wire carries several bel pins'
    ways, it takes the least of them (a LUT's input is its 6-LUT's, a
    clock the flip-flops')."""

    def __init__(self, timing, sv):
        self.t = timing
        self.st = sv.site_type()
        self.ins = defaultdict(list)  # site wire -> bel input pins on it
        self.outs = defaultdict(list)
        for bel in sv.bels():
            if bel.bel_class() != "BEL":
                continue
            for pin in bel.pins():
                (self.outs if pin.dir() == "OUTPUT" else self.ins)[pin.site_wire().name()].append(f"{self.st}/{bel.name()}/{pin.name}")

    def least(self, names):
        """The delays of the least (slow max) of these terms, or None."""
        known = [d for d in (self.t.timed(self.t.delays.get(n)) for n in names) if d]
        if not known:
            return None
        return min(known, key=lambda d: d[self.t.grades[0]][3])

    def way_in(self, wire):
        return self.least(f"in:{p}" for p in self.ins[wire])

    def way_out(self, wire):
        return self.least(f"out:{p}" for p in self.outs[wire])

    def named(self, delays):
        return self.t.pip_class(delays) if delays else self.t.zero()

    def entrance(self, wire):
        return self.named(self.way_in(wire))

    def exit(self, wire):
        return self.named(self.way_out(wire))

    def internal(self, src, dst):
        if self.outs[src] and self.ins[dst]:
            return self.named(self.least(f"inside:{d}>{s}" for d in self.outs[src] for s in self.ins[dst]))
        if self.ins[dst]:
            # The rest of the way from a site pin: its entrance has the
            # way into the bel pins on its own wire.
            into, before = self.way_in(dst), self.way_in(src)
            if into and before:
                into = {g: [max(0, a - b) for a, b in zip(into[g], before[g])] for g in self.t.grades}
            return self.named(into)
        return self.named(self.way_out(src))

    def route_through(self, lut, i):
        """LUT `lut`'s input `i` (`A`, 3) to its 6-LUT's output, out of the
        site: in, through, out."""
        parts = [self.way_in(f"{lut}{i}"), self.least([f"out:{self.st}/{lut}6LUT/O6"])]
        arcs = {}
        for g, entries in self.t.cells.get(("LUT6", f"{self.st}/{lut}6LUT"), {}).items():
            for e in entries:
                if isinstance(e, parse_sdf.IOPath) and e.from_pin == f"A{i}" and e.to_pin == "O6":
                    arcs[g] = corners(e)
        if None in parts or not self.t.timed(arcs):
            return self.t.zero()
        return self.t.pip_class({g: [parts[0][g][k] + parts[1][g][k] + arcs[g][k] for k in range(4)] for g in self.t.grades})
