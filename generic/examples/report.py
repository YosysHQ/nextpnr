with open("blinky.txt", "w") as f:
	for nname, net in ctx.nets:
		print("# Net %s" % nname, file=f)
		# FIXME: Pip ordering
		for wire, pip in net.wires:
			if pip.pip != "":
				print("%s" % pip.pip, file=f)
		print("", file=f)
	for cname, cell in ctx.cells:
		print("# Cell %s at %s" % (cname, cell.bel), file=f)
		for param, val in cell.params:
			print("%s.%s %s" % (cell.bel, param, val), file=f)
		print("", file=f)