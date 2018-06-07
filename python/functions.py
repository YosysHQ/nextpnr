def get_drivers(wire):
    wid = chip.getWireByName(wire)
    assert not wid.nil(), "wire {} not found".format(wire)
    bp = chip.getBelPinUphill(wid)
    if not bp.bel.nil():
        print("Bel pin: {}.{}".format(chip.getBelName(bp.bel), str(bp.pin)))
    for pip in sorted(chip.getPipsUphill(wid), key=lambda x: x.index):
        print("Pip: {}".format(chip.getWireName(chip.getPipSrcWire(pip))))


def get_loads(wire):
    wid = chip.getWireByName(wire)
    assert not wid.nil(), "wire {} not found".format(wire)
    for bp in sorted(chip.getBelPinsDownhill(wid), key=lambda x: (x.bel.index, x.pin)):
        print("Bel pin: {}.{}".format(chip.getBelName(bp.bel), str(bp.pin)))
    for pip in sorted(chip.getPipsDownhill(wid), key=lambda x: x.index):
        print("Pip: {}".format(chip.getWireName(chip.getPipDstWire(pip))))


#get_drivers("12_14_lutff_7/in_3")
#get_loads("12_14_lutff_global/clk")
