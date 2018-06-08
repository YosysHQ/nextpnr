# Run: PYTHONPATH=. python3 python/python_mod_test.py
from nextpnrpy_ice40 import Chip, ChipArgs, iCE40Type
args = ChipArgs()
args.type = iCE40Type.HX1K
chip = Chip(args)
for wire in chip.getWires():
    print(chip.getWireName(wire))
