for cname, cell in ctx.cells:
    if cell.type != "GENERIC_SLICE":
        continue
    if cname in ("$PACKER_GND", "$PACKER_VCC"):
        continue
    K = int(cell.params["K"])
    ctx.addCellTimingClock(cell=cname, port="CLK")
    for i in range(K):
        ctx.addCellTimingSetupHold(cell=cname, port="I[%d]" % i, clock="CLK",
            setup=ctx.getDelayFromNS(0.2), hold=ctx.getDelayFromNS(0))
    ctx.addCellTimingClockToOut(cell=cname, port="Q", clock="CLK", clktoq=ctx.getDelayFromNS(0.2))
    for i in range(K):
        ctx.addCellTimingDelay(cell=cname, fromPort="I[%d]" % i, toPort="F", delay=ctx.getDelayFromNS(0.2))
