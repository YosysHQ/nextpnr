from nextpnrpy_ice40 import Chip, ChipArgs, iCE40Type
args = ChipArgs()
args.type = iCE40Type.LP384
chip = Chip(args)
for wire in chip.getWires():
    print(chip.getWireName(wire))
