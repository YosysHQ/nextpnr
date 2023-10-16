"""
Utilities for SDF file parsing to determine cell timings
"""
import sys


class SDFData:
    def __init__(self):
        self.cells = {}

class Delay:
    def __init__(self, minv, typv, maxv):
        self.minv = minv
        self.typv = typv
        self.maxv = maxv


class IOPath:
    def __init__(self, from_pin, to_pin, rising, falling):
        self.from_pin = from_pin
        self.to_pin = to_pin
        self.rising = rising
        self.falling = falling


class SetupHoldCheck:
    def __init__(self, pin, clock, setup, hold):
        self.pin = pin
        self.clock = clock
        self.setup = setup
        self.hold = hold


class WidthCheck:
    def __init__(self, clock, width):
        self.clock = clock
        self.width = width


class Interconnect:
    def __init__(self, from_net, to_net, rising, falling):
        self.from_net = from_net
        self.to_net = to_net
        self.rising = rising
        self.falling = falling


class CellData:
    def __init__(self, celltype, inst):
        self.type = celltype
        self.inst = inst
        self.entries = []
        self.interconnect = {}


def parse_sexpr(stream):
    content = []
    buffer = ""
    instr = False
    while True:
        c = stream.read(1)
        assert c != "", "unexpected end of file"
        if instr:
            if c == '"':
                instr = False
            else:
                buffer += c
        else:
            if c == '(':
                content.append(parse_sexpr(stream))
            elif c == ')':
                if buffer != "":
                    content.append(buffer)
                return content
            elif c.isspace():
                if buffer != "":
                    content.append(buffer)
                buffer = ""
            elif c == '"':
                instr = True
            else:
                buffer += c


def parse_sexpr_file(filename):
    with open(filename, 'r') as f:
        c = f.read(1)
        while c != '(':
            assert c == ' ' or c == '\n' or c == '\t'
            c = f.read(1)
        return parse_sexpr(f)


def parse_delay(delay):
    sp = [float(x) if x != '' else None for x in delay.split(":")]
    assert len(sp) == 3
    return Delay(sp[0], sp[1], sp[2])


def parse_sdf_file(filename):
    sdata = parse_sexpr_file(filename)
    assert sdata[0] == "DELAYFILE"
    sdf = SDFData()
    for entry in sdata[1:]:
        if entry[0] != "CELL":
            continue
        assert entry[1][0] == "CELLTYPE"
        celltype = entry[1][1]
        assert entry[2][0] == "INSTANCE"
        if len(entry[2]) > 1:
            inst = entry[2][1]
        else:
            inst = "top"
        cell = CellData(celltype, inst)
        for subentry in entry[3:]:
            if subentry[0] == "DELAY":
                assert subentry[1][0] == "ABSOLUTE"
                for delay in subentry[1][1:]:
                    if delay[0] == "IOPATH":
                        cell.entries.append(
                            IOPath(delay[1], delay[2], parse_delay(delay[3][0]), parse_delay(delay[4][0])))
                    elif delay[0] == "INTERCONNECT":
                        cell.interconnect[(delay[1], delay[2])] = Interconnect(delay[1], delay[2],
                                                                               parse_delay(delay[3][0]),
                                                                               parse_delay(delay[4][0]))
            elif subentry[0] == "TIMINGCHECK":
                for check in subentry[1:]:
                    if check[0] == "SETUPHOLD":
                        cell.entries.append(
                            SetupHoldCheck(check[1], check[2], parse_delay(check[3][0]), parse_delay(check[4][0])))
                    elif check[0] == "WIDTH":
                        cell.entries.append(WidthCheck(check[1], parse_delay(check[2][0])))
        sdf.cells[(celltype, inst)] = cell
    return sdf
