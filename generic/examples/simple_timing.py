for cname, cell in ctx.cells:
	if cell.type != "GENERIC_SLICE":
		continue
	if cname in ("$PACKER_GND", "$PACKER_VCC"):
		continue
	K = int(cell.params["K"])
	if int(cell.params["FF_USED"], 2) == 1:
		ctx.addCellTimingClock(cell=cname, port="CLK")
		for i in range(K):
			ctx.addCellTimingSetupHold(cell=cname, port="I[%d]" % i, clock="CLK",
				setup=ctx.getDelayFromNS(0.2), hold=ctx.getDelayFromNS(0))
		ctx.addCellTimingClockToOut(cell=cname, port="Q", clock="CLK", clktoq=ctx.getDelayFromNS(0.2))
	else:
		for i in range(K):
			ctx.addCellTimingDelay(cell=cname, fromPort="I[%d]" % i, toPort="Q", delay=ctx.getDelayFromNS(0.2))