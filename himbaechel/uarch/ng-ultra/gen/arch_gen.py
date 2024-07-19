import json
from os import path
import sys
import argparse
import gzip

# Configuration flags
USE_LUT_PERMUTATION = True

sys.path.append(path.join(path.dirname(__file__), "../../.."))
from himbaechel_dbgen.chip import *

@dataclass
class TileExtraData(BBAStruct):
    name: IdString
    lobe: int = 0
    tile_type: int = 0

    def serialise_lists(self, context: str, bba: BBAWriter):
        pass
    def serialise(self, context: str, bba: BBAWriter):
        bba.u32(self.name.index)
        bba.u8(self.lobe)
        bba.u8(self.tile_type)
        bba.u16(0) # dummy

@dataclass
class PipExtraData(BBAStruct):
    name: IdString = field(default_factory=IdString)
    data_type: int = 0
    input: int = 0
    output: int = 0

    def serialise_lists(self, context: str, bba: BBAWriter):
        pass
    def serialise(self, context: str, bba: BBAWriter):
        bba.u32(self.name.index)
        bba.u16(self.data_type)
        bba.u8(self.input)
        bba.u8(self.output)

@dataclass
class BelExtraData(BBAStruct):
    flags: int = 0

    def serialise_lists(self, context: str, bba: BBAWriter):
        pass
    def serialise(self, context: str, bba: BBAWriter):
        bba.u32(self.flags)
 
PLL_Z = 0
WFG_C_Z = 1
WFG_R_Z = 11

IOM_Z = 136

RAM_Z = 0
DSP1_Z = 1
DSP2_Z = 2

SOCIF_Z = 0
SERVICE_Z = 1

LUT_Z = 0
CY_Z = 32
XLUT_Z = CY_Z + 4
RF_Z =  XLUT_Z + 8
XRF_Z = RF_Z + 2
FIFO_Z = XRF_Z + 1
XFIFO_Z = FIFO_Z + 2
CDC_Z = XFIFO_Z + 1
XCDC_Z = CDC_Z + 2

PIP_EXTRA_CROSSBAR = 1
PIP_EXTRA_MUX = 2
PIP_EXTRA_BYPASS = 3
PIP_EXTRA_LUT_PERMUTATION = 4
PIP_EXTRA_INTERCONNECT = 5
PIP_EXTRA_VIRTUAL = 6

BEL_EXTRA_FE_CSC = 1
BEL_EXTRA_FE_SCC = 2

TILE_EXTRA_FABRIC = 0
TILE_EXTRA_TUBE = 1
TILE_EXTRA_SOC = 2
TILE_EXTRA_RING = 3
TILE_EXTRA_FENCE = 4

def bel_z(tile_type, bel_name, bel_type):
    if tile_type.startswith("CKG"):
        if (bel_type=="PLL"):
            return PLL_Z
        else:
            if bel_name.startswith("WFG.WFG_C"):
                return int(bel_name.replace("WFG.WFG_C","")) + WFG_C_Z -1
            else:
                return int(bel_name.replace("WFG.WFG_R","")) + WFG_R_Z -1
    elif tile_type.startswith("HSSL"):
        if bel_type=="PMA":
            return 0
        elif bel_type=="CRX":
            return int(bel_name.split(".")[0][7:]) * 2 - 1
        else:
            return int(bel_name.split(".")[0][7:]) * 2
    elif tile_type.startswith("IOB") and tile_type in ["IOB0","IOB1","IOB6","IOB7"]: #direct
        sp = bel_name.split(".")
        if bel_type=="IOP":
            return int(sp[0][1:]) * 4
        else:
            return (int(sp[0][3:])-1) * 4 + (1 if sp[1][0]=='I' else 2 if sp[1][0]=='O' else 3)
    elif tile_type.startswith("IOB"): # complex
        sp = bel_name.split(".")
        if bel_type=="IOTP":
            return (int(sp[0][1:3]) *2 + (1 if sp[0][3]=='N' else 0)) * 4
        elif bel_type=="IOM":
            return IOM_Z
        else:
            return (int(sp[0][3:])-1) * 4 + (1 if sp[1][0]=='I' else 2 if sp[1][0]=='O' else 3)
    elif tile_type == "CGB":
        if bel_type=="RAM":
            return RAM_Z
        elif bel_name=="S2.DSP1":
            return DSP1_Z
        else:
            return DSP2_Z
    elif tile_type == "SOCBank":
        if bel_type=="SOCIF":
            return SOCIF_Z
        else:
            return SERVICE_Z
    elif tile_type == "TILE":
        sp = bel_name.split(".")
        if bel_type=="BEYOND_FE":
            return (int(sp[1][2:])-1) % 32
        elif bel_type=="CY":
            pos = int(sp[1][2:])-1
            # in S1, S5 and S9 they are ordered other way arround
            return ((pos % 4) if pos < 12 else 3 - (pos % 4)) + CY_Z
        elif bel_type=="XLUT":
            return (int(sp[1][4:])-1) % 8 + XLUT_Z
        elif bel_type=="RF":
            return (int(sp[1][2:])-1) + RF_Z
        elif bel_type=="XRF":
            return (int(sp[1][3:])-1) + XRF_Z
        elif bel_type=="FIFO":
            return (int(sp[1][4:])-1) + FIFO_Z
        elif bel_type=="XFIFO":
            return (int(sp[1][5:])-1) + XFIFO_Z
        elif bel_type=="CDC":
            return (int(sp[1][3:])-1) + CDC_Z
        elif bel_type=="XCDC":
            return (int(sp[1][4:])-1) + XCDC_Z
        else:
            raise Exception(f"Unknown bel type {bel_type}")
    elif tile_type == "TUBE":
        sp = bel_name.split(".")
        return (int(sp[0][1:])-1)*20 + (int(sp[1][1:])-1)
    else:
        raise Exception(f"Unknown type {tile_type}")

if USE_LUT_PERMUTATION:
    # Note PI1-PI4 are not real inputs but will appear
    # before actual BEL input to enable LUT permutation
    lut_to_beyond_fe = {
        "I1" : "PI1",
        "I2" : "PI2",
        "I3" : "PI3",
        "I4" : "PI4",
        "O"  : "LO",
        "J"  : "LJ",
        "K"  : "LK",
        "D"  : "LD",
        "X"  : "LX",
    }
else:
    lut_to_beyond_fe = {
        "I1" : "I1",
        "I2" : "I2",
        "I3" : "I3",
        "I4" : "I4",
        "O"  : "LO",
        "J"  : "LJ",
        "K"  : "LK",
        "D"  : "LD",
        "X"  : "LX",
    }

dff_to_beyond_fe = {
    "I"  : "DI",
    "O"  : "DO",
    "L"  : "L",
    "CK" : "CK",
    "R"  : "R",
    "J"  : "DJ",
    "P"  : "DP",
    "C"  : "DC",
    "S"  : "DS",
    "K"  : "DK",
}

def is_complex(tile_type):
    return tile_type not in ["IOB0","IOB1","IOB6","IOB7"]

def map_to_beyond(tile_type,bel_name,bel_type=None):
    if not tile_type in ["TILE"]:
        return (bel_name,bel_type)
    s = bel_name.split(".")
    if tile_type in ["TILE"]:
        # BEYOND_FE
        if not (s[1].startswith("LUT") or s[1].startswith("DFF")):
            return (bel_name,bel_type)
        if len(s)==3:
            # BEL and PORT
            if s[1].startswith("LUT"):
                pin = lut_to_beyond_fe[s[2]]
            else:
                pin = dff_to_beyond_fe[s[2]]
            if pin is None:
                return (None,bel_type)
            return (s[0]+".FE"+s[1][3:]+"."+pin, bel_type if type(bel_type) is int else "BEYOND_FE")
        else:
            # just BEL
            return (s[0]+".FE"+s[1][3:], "BEYOND_FE")

split_map = ["TILE", "CGB"]

def split_tilegrid(tilegrid):
    new_tilegrid = dict()
    for k,v in tilegrid.items():
        if v["type"] not in split_map:
            new_tilegrid[k] = dict()
            data = dict(v)
            data["orig"] = data["type"]
            data["x"]=data["x"]*4
            data["y"]=data["y"]*4
            new_tilegrid[k][0] = data
        else:
            new_tilegrid[k] = dict()
            for i in range(4*4):
                data = dict(v)
                data["orig"] = data["type"]
                data["type"] = data["type"]+"_"+str(i)
                data["x"]=data["x"]*4 + i // 4
                data["y"]=data["y"]*4 + i % 4
                new_tilegrid[k][i] = data
    return new_tilegrid

def determine_subtile(tile,bel):
    if tile=="TILE":
        if bel.startswith("SYSTEM"):
            s = 3
        elif bel.startswith("RE") or bel.startswith("RS") or bel.startswith("RI"):
            s = int(bel.split(".")[0][2:])-1
        elif bel.startswith("S"):
            s = int(bel.split(".")[0][1:])+3
        else:
            return 0
        return s
    elif tile=="CGB":
        if bel.startswith("SYSTEM"):
            s = 6
        elif bel.startswith("RE"):
            s = 8
        elif bel.startswith("RS"):
            s = int(bel.split(".")[0][2:])
        elif bel.startswith("RI"):
            s = 5
        elif bel.startswith("S1"):
            s = 0
        elif bel.startswith("S2.DSP1"):
            s = 7
        else:
            return 15
        return s
    else:
        return 0

def split_per_bels(bels):
    new_bels = dict()
    for k,v in bels.items():
        if k not in split_map:
            new_bels[k] = dict()
            data = dict(v)
            new_bels[k][0] = data
        else:
            new_bels[k] = dict()
            for i in range(4*4):
                new_bels[k][i] = dict()
            for item,bel in v.items():
                (item, bel) = map_to_beyond(k,item,bel)
                if item is not None:
                    num = determine_subtile(k,item)
                    new_bels[k][num][item] = bel
    return new_bels

def lookup_port_type(t):
    if t == "Input": return PinType.INPUT
    elif t == "Output": return PinType.OUTPUT
    elif t == "Bidir": return PinType.INOUT
    else: assert False

def create_pips(tt, tile_type, muxes, num, args):
    file_path = path.join(args.db, args.device, tile_type + ".txt")
    if not path.isfile(file_path):
        return
    with open(file_path) as f:
        for item in f:
            line = item.strip().split(" ")
            name1,_ = map_to_beyond(tile_type,line[0])
            name2,_ = map_to_beyond(tile_type,line[1])
            if name1 is None or name2 is None:
                continue
            num1 = determine_subtile(tile_type,name1)
            num2 = determine_subtile(tile_type,name2)

            if name2 in muxes:
                name2 = name2 + "." + line[2]
            if num1==num and not tt.has_wire(name1):
                tt.create_wire(name=name1, type=tile_type + "_WIRE")
            if num2==num and not tt.has_wire(name2):
                tt.create_wire(name=name2, type=tile_type + "_WIRE")
            timing_class = line[3]
            # Only create PIP if both ends are in same subtile
            if num1==num and num2==num:
                tt.create_pip(name1,name2,timing_class)

def create_tile_types(ch: Chip, bels, bel_pins, crossbars, interconnects, muxes, args):
    for tile_type,items in bels.items():
        for num in items.keys():
            if len(items)==1:
                sub_type = tile_type
            else:
                sub_type = tile_type + "_"+str(num)
            tt = ch.create_tile_type(sub_type)

            def lookup_site_wire(canon_name):
                if not tt.has_wire(canon_name):
                    tt.create_wire(name=canon_name, type="BEL_PIN_WIRE")
                return canon_name

            # Create BELs inside tile
            for name,bel in bels[tile_type][num].items():
                nb = tt.create_bel(name, bel, z=bel_z(tile_type,name,bel))
                # Create wires for each BEL port
                for index in bel_pins[bel].keys():
                    pin = bel_pins[bel][index]
                    tt.add_bel_pin(nb, pin["name"], lookup_site_wire(f"{name}."+pin["name"]), lookup_port_type(pin["direction"]))
                if (tile_type.startswith("TILE") and bel=="BEYOND_FE"):
                    flag = 0
                    fe_nxd = int(name[name.index(".")+3:])
                    if (((fe_nxd-1) & 127) < 64):
                        if ((fe_nxd-1)&31) < 16:
                            if (fe_nxd & 1)==1:
                                flag |= BEL_EXTRA_FE_SCC
                        else:
                            if (fe_nxd & 1)==0:
                                flag |= BEL_EXTRA_FE_SCC

                    if fe_nxd in (65, 80, 81, 96, 225,240,241,256, 321,336,337,352):
                        flag |= BEL_EXTRA_FE_CSC
                    nb.extra_data = BelExtraData(flag)

                    # LUT drawers, LO already connected to LJ
                    vi = tt.create_pip(f"{name}.LJ",f"{name}.LK","Virtual")
                    vi.extra_data = PipExtraData(ch.strs.id(name),PIP_EXTRA_VIRTUAL,0,0)
                    vi = tt.create_pip(f"{name}.LJ",f"{name}.LD","Virtual")
                    vi.extra_data = PipExtraData(ch.strs.id(name),PIP_EXTRA_VIRTUAL,0,0)
                    vi = tt.create_pip(f"{name}.LJ",f"{name}.LX","Virtual")
                    vi.extra_data = PipExtraData(ch.strs.id(name),PIP_EXTRA_VIRTUAL,0,0)
                    # DFF drawers, DO already connected to DJ
                    vi = tt.create_pip(f"{name}.DJ",f"{name}.DP","Virtual")
                    vi.extra_data = PipExtraData(ch.strs.id(name),PIP_EXTRA_VIRTUAL,0,0)
                    vi = tt.create_pip(f"{name}.DJ",f"{name}.DC","Virtual")
                    vi.extra_data = PipExtraData(ch.strs.id(name),PIP_EXTRA_VIRTUAL,0,0)
                    vi = tt.create_pip(f"{name}.DJ",f"{name}.DS","Virtual")
                    vi.extra_data = PipExtraData(ch.strs.id(name),PIP_EXTRA_VIRTUAL,0,0)
                    vi = tt.create_pip(f"{name}.DJ",f"{name}.DK","Virtual")
                    vi.extra_data = PipExtraData(ch.strs.id(name),PIP_EXTRA_VIRTUAL,0,0)
                    # DFF bypass
                    by = tt.create_pip(f"{name}.DI",f"{name}.DO","BYPASS")
                    by.extra_data = PipExtraData(ch.strs.id(name),PIP_EXTRA_BYPASS,0,0)
                    # LUT bypass
                    by = tt.create_pip(f"{name}.I1",f"{name}.LO","BYPASS")
                    by.extra_data = PipExtraData(ch.strs.id(name),PIP_EXTRA_BYPASS,1,0)
                elif (tile_type.startswith("TILE") and bel=="XLUT"):
                    for out in ["G1","G2","G3","G4"]:
                        vi = tt.create_pip(f"{name}.J",f"{name}.{out}","Virtual")
                        vi.extra_data = PipExtraData(ch.strs.id(name),PIP_EXTRA_VIRTUAL,0,0)
                elif (tile_type.startswith("TILE") and bel=="CY"):
                    # matrix of each input to each output combination
                    # crossbar but use mux placeholder for convenience
                    for inp in ["I1","I2","I3","I4"]:
                        for out in ["O1","O2","O3","O4"]:
                            pd = tt.create_pip(f"{name}."+inp,f"{name}."+out,"MATRIX_PIP")
                            pd.extra_data = PipExtraData(ch.strs.id(f"{name}."+inp),PIP_EXTRA_MUX,int(inp[1:])-1,int(out[1:])-1)

                elif (tile_type.startswith("CKG") and bel=="WFG"):
                    by = tt.create_pip(f"{name}.ZI",f"{name}.ZO","BYPASS")
                    by.extra_data = PipExtraData(ch.strs.id(name),PIP_EXTRA_BYPASS,0,0)
                elif (tile_type.startswith("TUBE") and bel=="GCK"):
                    # 20 clock signals comming to 20 GCK, SI1 is bypass
                    by = tt.create_pip(f"{name}.SI1",f"{name}.SO","BYPASS")
                    by.extra_data = PipExtraData(ch.strs.id(name),PIP_EXTRA_BYPASS,0,0)
                    # there are CMD signals that can be bypassed as well
                    by = tt.create_pip(f"{name}.CMD",f"{name}.SO","BYPASS")
                    by.extra_data = PipExtraData(ch.strs.id(name),PIP_EXTRA_BYPASS,1,0)
                

            # Add LUT permutation
            if USE_LUT_PERMUTATION and tile_type=="TILE":
                for name,bel in bels[tile_type][num].items():
                    if bel=="BEYOND_FE":
                        for inp in ["PI1","PI2","PI3","PI4"]:
                            tt.create_wire(name=f"{name}."+inp, type="LUT_PERMUTATION_WIRE")
                            for out in ["I1","I2","I3","I4"]:
                                pd = tt.create_pip(f"{name}."+inp,f"{name}."+out,"LUT_PERMUTATION")
                                pd.extra_data = PipExtraData(ch.strs.id(name),PIP_EXTRA_LUT_PERMUTATION,int(inp[2:])-1,int(out[1:])-1)

            # Create crossbars as multiple PIPs
            for name,xb in crossbars[tile_type][num].items():
                inputs = list()
                outputs = list()
                for index in bel_pins[xb].keys():
                    pin = bel_pins[xb][index]
                    tt.create_wire(name=f"{name}."+pin["name"], type="CROSSBAR_"+xb+"_INPUT_WIRE" if pin["direction"] == "Input" else "CROSSBAR_"+xb+"_OUTPUT_WIRE")
                for index in bel_pins[xb].keys():
                    pin = bel_pins[xb][index]
                    if pin["direction"] == "Input":
                        inputs.append(pin["name"])
                    else:
                        outputs.append(pin["name"])
                for inp in inputs:
                    for out in outputs:
                        pd = tt.create_pip(f"{name}."+inp,f"{name}."+out,"CROSSBAR_"+xb)
                        pd.extra_data = PipExtraData(ch.strs.id(name),PIP_EXTRA_CROSSBAR,int(inp[1:])-1,int(out[1:])-1)

            # Interconnects are just PIPs with one I and one O
            for name,xb in interconnects[tile_type][num].items():
                for index in bel_pins[xb].keys():
                    pin = bel_pins[xb][index]
                    tt.create_wire(name=f"{name}."+pin["name"], type="INTERCONNECT_INPUT" if pin["direction"] == "Input" else "INTERCONNECT_OUTPUT")
                inp = f"{name}."+bel_pins[xb]["I"]["name"]
                out = f"{name}."+bel_pins[xb]["O"]["name"]
                pd = tt.create_pip(inp,out,"INTERCONNECT")
                pd.extra_data = PipExtraData(ch.strs.id(name),PIP_EXTRA_INTERCONNECT,0,0)

            # Create MUXes as many to one connections
            if tile_type in muxes:
                for (name, n) in muxes[tile_type][num].items():
                    for i in range(0,n+1):
                        new_name = name + "." + str(i)
                        if not tt.has_wire(new_name):
                            tt.create_wire(name=new_name, type="MUX_WIRE")
                        pd = tt.create_pip(new_name,name,"MUX_PIP")
                        pd.extra_data = PipExtraData(ch.strs.id(name),PIP_EXTRA_MUX,i,0)

            m = muxes[tile_type] if tile_type in muxes else dict()                
            mux = muxes[tile_type][num] if num in m else dict()
            create_pips(tt, tile_type, mux, num, args)

def create_null(ch: Chip):
    tt = ch.create_tile_type("NULL")

def set_timings(ch, pip_timings, bel_timings):
    speed = "DEFAULT"
    tmg = ch.set_speed_grades([speed])
    for k,v in pip_timings.items():
        tmg.set_pip_class(grade=speed, name=k, delay=TimingValue(v[0]))
        tmg.set_node_class(grade=speed, name=k, delay=TimingValue(v[0]))
    tmg.set_pip_class(grade=speed, name="INTERCONNECT", delay=TimingValue(0))
    tmg.set_pip_class(grade=speed, name="MUX_PIP", delay=TimingValue(0))
    tmg.set_pip_class(grade=speed, name="MATRIX_PIP", delay=TimingValue(0))
    tmg.set_pip_class(grade=speed, name="LUT_PERMUTATION", delay=TimingValue(0))
    tmg.set_pip_class(grade=speed, name="BYPASS", delay=TimingValue(142))
    for k,g in bel_timings.items():
        primitive = ch.timing.add_cell_variant(speed, k)
        for t,v in g.items():
            if t=="IOPath":
                for from_port,values in v.items():
                    for to_port,data in values.items():
                        primitive.add_comb_arc(from_port, to_port, TimingValue(data[0], data[1]))
            elif t=="SetupHold":
                for from_port,values in v.items():
                    for to_port,data in values.items():
                        primitive.add_setup_hold(from_port, to_port, ClockEdge.RISING, TimingValue(data[0]), TimingValue(data[1]))
            elif t=="ClockOut":
                for from_port,values in v.items():
                    for to_port,data in values.items():
                        primitive.add_clock_out(from_port, to_port, ClockEdge.RISING, TimingValue(data[0],data[1]))

def get_pos(tilegrid,name,bel):
    tile = tilegrid[name][0]["orig"]
    num = determine_subtile(tile,bel)
    item = tilegrid[name][num]
    x = item["x"]
    y = item["y"]
    return (tile,num,x,y)


global_connections = dict()
def load_globals(args):
    print("Load global connections...")
    with gzip.open(path.join(args.db, args.device, "GLOBAL.txt.gz"),"rt") as f:
        for item in f:
            line = item.strip().split(" ")
            tile_name = line[0].split(":")[0]
            if tile_name not in global_connections:
                global_connections[tile_name] = dict()
            if line[0] not in global_connections[tile_name]:
                global_connections[tile_name][line[0]] = list()
            global_connections[tile_name][line[0]].append(line)

def create_nodes(ch, tile_name, tilegrid, muxes, pip_timings):
    if tile_name not in global_connections:
        return
    connections = global_connections[tile_name]
    for key,val in connections.items():
        name1 = key.split(":")
        t1,_,x1,y1 = get_pos(tilegrid,name1[0],name1[1])
        name1[1],_ = map_to_beyond(t1,name1[1])
        if name1[1] is None:
            continue
        node = [NodeWire(x1, y1, name1[1])]
        timing = None
        timing_val = -1
        for v in val:
            name2 = v[1].split(":")
            name2[1],_ = map_to_beyond(tilegrid[name2[0]][0]["orig"],name2[1])
            t2,num,x2,y2 = get_pos(tilegrid,name2[0],name2[1])
            if name2[1] is None:
                continue
            if pip_timings[v[3]][0] > timing_val:
                timing_val = pip_timings[v[3]][0]
                timing = v[3]
            if t2 in muxes and num in muxes[t2] and name2[1] in muxes[t2][num]:
                node.append(NodeWire(x2, y2, name2[1]+"."+v[2]))
            else:
                node.append(NodeWire(x2, y2, name2[1]))

        ch.add_node(node,timing_class=timing)


subtile_connections = dict()
def create_nodes_subtiles(ch, tilegrid, name, tile_type, muxes, pip_timings, args):
    if tile_type not in subtile_connections:
        subtile_connections[tile_type] = dict()

        file_path = path.join(args.db, args.device, tile_type + ".txt")
        if not path.isfile(file_path):
            return
        with open(file_path) as f:
            for item in f:
                line = item.strip().split(" ")
                name1 = line[0]
                name2 = line[1]
                name1,_ = map_to_beyond(tile_type,line[0])
                name2,_ = map_to_beyond(tile_type,line[1])
                if name1 is None or name2 is None:
                    continue
                num1 = determine_subtile(tile_type,name1)
                num2 = determine_subtile(tile_type,name2)
                # Only create WIRE if ends are NOT in same subtile
                if num1!=num2:
                    if name1 not in subtile_connections[tile_type] :
                        subtile_connections[tile_type][name1] = list()
                    subtile_connections[tile_type][name1].append([name1, name2, line[2], line[3]])

    for name1,val in subtile_connections[tile_type].items():
        _,_,x1,y1 = get_pos(tilegrid,name,name1)
        node = [NodeWire(x1, y1, name1)]
        timing_val = -1
        timing = None
        for v in val:
            name2 = v[1]
            t2,num,x2,y2 = get_pos(tilegrid,name,name2)
            if pip_timings[v[3]][0] > timing_val:
                timing_val = pip_timings[v[3]][0]
                timing = v[3]
            if t2 in muxes and num in muxes[t2] and name2 in muxes[t2][num]:
                node.append(NodeWire(x2, y2, name2+"."+v[2]))
            else:
                node.append(NodeWire(x2, y2, name2))

        ch.add_node(node,timing_class=timing)

def import_package(ch, package, bels, tilegrid):
    pkg = ch.create_package(package)
    for name, data in tilegrid.items():
        for key, item in data.items():
            ty = item["orig"]
            x = item["x"]
            y = item["y"]
            if ty.startswith("IOB"):
                for bel,ty in bels[ty][key].items():
                    # Support for native primitives
                    if ty =="IOTP":
                        pin = name+"_"+bel[:4]
                    elif ty =="IOP":
                        pin = name+"_"+bel[:3]
                    else:
                        continue
                    pkg.create_pad(pin, f"X{x}Y{y}", bel, "", int(name[3:]))

def main():
    xlbase = path.join(path.dirname(path.realpath(__file__)), "..")

    parser = argparse.ArgumentParser()
    parser.add_argument("--db", help="Project Beyond device database path (e.g. ../prjbeyond/database)", type=str, required=True)
    parser.add_argument("--device", help="name of device to export", type=str, required=True)
    parser.add_argument("--constids", help="name of nextpnr constids file to read", type=str, default=path.join(xlbase, "constids.inc"))
    parser.add_argument("--bba", help="bba file to write", type=str, required=True)
    args = parser.parse_args()

    with open(path.join(args.db, "devices.json")) as f:
        devices = json.load(f)

    width = (devices["families"][args.device]["max_col"] + 1) * 4
    height = (devices["families"][args.device]["max_row"] + 1) * 4
    packages = devices["families"][args.device]["packages"]

    ch = Chip("ng-ultra",args.device, width, height)
    ch.strs.read_constids(path.join(path.dirname(__file__), "..", "constids.inc"))

    # Data that is depending of location
    with open(path.join(args.db, args.device, "tilegrid.json")) as f:
        tilegrid = split_tilegrid(json.load(f))

    with open(path.join(args.db, args.device, "bels.json")) as f:
        bels = split_per_bels(json.load(f))

    with open(path.join(args.db, args.device, "crossbars.json")) as f:
        crossbars = split_per_bels(json.load(f))

    with open(path.join(args.db, args.device, "interconnects.json")) as f:
        interconnects = split_per_bels(json.load(f))

    with open(path.join(args.db, args.device, "muxes.json")) as f:
        muxes = split_per_bels(json.load(f))

    # Data that is not related to position
    with open(path.join(args.db, args.device, "bel_pins.json")) as f:
        bel_pins = json.load(f)
        bel_pins["IOP"]["IO"] = { "direction": "Bidir", "name": "IO" }
        bel_pins["IOTP"]["IO"] = { "direction": "Bidir", "name": "IO" }

    with open(path.join(args.db, args.device, "pip_timings.json")) as f:
        pip_timings = json.load(f)

    with open(path.join(args.db, args.device, "bel_timings.json")) as f:
        bel_timings = json.load(f)

    create_tile_types(ch, bels, bel_pins, crossbars, interconnects, muxes, args)
    create_null(ch)
    set_timings(ch, pip_timings, bel_timings)

    for x in range(width):
        for y in range(height):
            ch.set_tile_type(x,y,"NULL")

    load_globals(args)
    for name, data in tilegrid.items():
        for item in data.values():
            ti = ch.set_tile_type(item["x"],item["y"],item["type"])
            lobe = 0
            if item["orig"] in ["TILE","CGB"]:
                tmp = name.replace("TILE[","").replace("CGB[","").replace("]","")
                x,y = tmp.split("x")
                lobe = ((int(y)-1) // 12)*2 + (1 if int(x)>46 else 0) + 1
            elif item["orig"].startswith("IOB") or item["orig"].startswith("HSSL"):
                match item["orig"]:
                    case "IOB0" | "IOB1":
                        lobe = 5
                    case "IOB6" | "IOB7":
                        lobe = 6
                    case "IOB8" | "IOB9" | "IOB10":
                        lobe = 2
                    case "IOB11" | "IOB12" | "IOB13":
                        lobe = 1
                    case "IOB2" | "IOB3" | "HSSL0" | "HSSL1" | "HSSL2" | "HSSL3":
                        lobe = 7
                    case "IOB4" | "IOB5" | "HSSL4" | "HSSL5" | "HSSL6" | "HSSL7":
                        lobe = 8
            tile_type = 0
            if item["orig"] in ["TILE","CGB","MESH"]:
                tile_type = TILE_EXTRA_FABRIC
            elif item["orig"] in ["TUBE"]:
                tile_type = TILE_EXTRA_TUBE
            elif item["orig"] in ["SOCBank"]:
                tile_type = TILE_EXTRA_SOC
            elif item["orig"].startswith("IOB") or item["orig"].startswith("HSSL") or item["orig"].startswith("CKG"):
                tile_type = TILE_EXTRA_RING
            elif item["orig"].startswith("FENCE"):
                tile_type = TILE_EXTRA_FENCE

            ti.extra_data = TileExtraData(ch.strs.id(name),lobe, tile_type)

    for name, data in tilegrid.items():
        print(f"Generate nodes for {name}...")
        create_nodes_subtiles(ch, tilegrid, name, data[0]["orig"], muxes, pip_timings, args)
        create_nodes(ch, name, tilegrid, muxes, pip_timings)

    for package in packages:
        import_package(ch, package, bels, tilegrid)

    ch.write_bba(args.bba)

if __name__ == '__main__':
    main()
