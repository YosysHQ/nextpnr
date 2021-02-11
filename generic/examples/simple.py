from simple_config import *

def is_io(x, y):
	return x == 0 or x == X-1 or y == 0 or y == Y-1

for x in range(X):
	for y in range(Y):
		# Bel port wires
		for z in range(N):
			ctx.addWire(name="X%dY%dZ%d_CLK" % (x, y, z), type="BEL_CLK", x=x, y=y)
			ctx.addWire(name="X%dY%dZ%d_Q" % (x, y, z), type="BEL_Q", x=x, y=y)
			ctx.addWire(name="X%dY%dZ%d_F" % (x, y, z), type="BEL_F", x=x, y=y)
			for i in range(K):
				ctx.addWire(name="X%dY%dZ%d_I%d" % (x, y, z, i), type="BEL_I", x=x, y=y)
		# Local wires
		for l in range(Wl):
			ctx.addWire(name="X%dY%d_LOCAL%d" % (x, y, l), type="LOCAL", x=x, y=y)
		# Create bels
		if is_io(x, y):
			if x == y:
				continue
			for z in range(2):
				ctx.addBel(name="X%dY%d_IO%d" % (x, y, z), type="GENERIC_IOB", loc=Loc(x, y, z), gb=False, hidden=False)
				ctx.addBelInput(bel="X%dY%d_IO%d" % (x, y, z), name="I", wire="X%dY%dZ%d_I0" % (x, y, z))
				ctx.addBelInput(bel="X%dY%d_IO%d" % (x, y, z), name="EN", wire="X%dY%dZ%d_I1" % (x, y, z))
				ctx.addBelOutput(bel="X%dY%d_IO%d" % (x, y, z), name="O", wire="X%dY%dZ%d_Q" % (x, y, z))
		else:
			for z in range(N):
				ctx.addBel(name="X%dY%d_SLICE%d" % (x, y, z), type="GENERIC_SLICE", loc=Loc(x, y, z), gb=False, hidden=False)
				ctx.addBelInput(bel="X%dY%d_SLICE%d" % (x, y, z), name="CLK", wire="X%dY%dZ%d_CLK" % (x, y, z))
				for k in range(K):
					ctx.addBelInput(bel="X%dY%d_SLICE%d" % (x, y, z), name="I[%d]" % k, wire="X%dY%dZ%d_I%d" % (x, y, z, k))
				ctx.addBelOutput(bel="X%dY%d_SLICE%d" % (x, y, z), name="F", wire="X%dY%dZ%d_F" % (x, y, z))
				ctx.addBelOutput(bel="X%dY%d_SLICE%d" % (x, y, z), name="Q", wire="X%dY%dZ%d_Q" % (x, y, z))

for x in range(X):
	for y in range(Y):
		# Pips driving bel input wires
		# Bel input wires are driven by every Si'th local with an offset
		def create_input_pips(dst, offset, skip):
			for i in range(offset % skip, Wl, skip):
				src = "X%dY%d_LOCAL%d" % (x, y, i)
				ctx.addPip(name="X%dY%d.%s.%s" % (x, y, src, dst), type="BEL_INPUT",
					srcWire=src, dstWire=dst, delay=ctx.getDelayFromNS(0.05), loc=Loc(x, y, 0))
		for z in range(N):
			create_input_pips("X%dY%dZ%d_CLK" % (x, y, z), 0, Si)
			for k in range(K):
				create_input_pips("X%dY%dZ%d_I%d" % (x, y, z, k), k % Si, Si)

		# Pips from bel outputs to locals
		def create_output_pips(dst, offset, skip):
			for i in range(offset % skip, N, skip):
				src = "X%dY%dZ%d_F" % (x, y, i)
				ctx.addPip(name="X%dY%d.%s.%s" % (x, y, src, dst), type="BEL_OUTPUT",
					srcWire=src, dstWire=dst, delay=ctx.getDelayFromNS(0.05), loc=Loc(x, y, 0))
				src = "X%dY%dZ%d_Q" % (x, y, i)
				ctx.addPip(name="X%dY%d.%s.%s" % (x, y, src, dst), type="BEL_OUTPUT",
					srcWire=src, dstWire=dst, delay=ctx.getDelayFromNS(0.05), loc=Loc(x, y, 0))
		# Pips from neighbour locals to locals
		def create_neighbour_pips(dst, nx, ny, offset, skip):
			if nx < 0 or nx >= X or ny < 0 or ny >= Y:
				return
			for i in range(offset % skip, Wl, skip):
				src = "X%dY%d_LOCAL%d" % (nx, ny, i)
				ctx.addPip(name="X%dY%d.%s.%s" % (x, y, src, dst), type="NEIGHBOUR",
					srcWire=src, dstWire=dst, delay=ctx.getDelayFromNS(0.05), loc=Loc(x, y, 0))
		for l in range(Wl):
			dst = "X%dY%d_LOCAL%d" % (x, y, l)
			create_output_pips(dst, l % Sq, Sq)
			create_neighbour_pips(dst, x-1, y-1, (l + 1) % Sl, Sl)
			create_neighbour_pips(dst, x-1, y, (l + 2) % Sl, Sl)
			create_neighbour_pips(dst, x-1, y+1, (l + 2) % Sl, Sl)
			create_neighbour_pips(dst, x, y-1, (l + 3) % Sl, Sl)
			create_neighbour_pips(dst, x, y+1, (l + 4) % Sl, Sl)
			create_neighbour_pips(dst, x+1, y-1, (l + 5) % Sl, Sl)
			create_neighbour_pips(dst, x+1, y, (l + 6) % Sl, Sl)
			create_neighbour_pips(dst, x+1, y+1, (l + 7) % Sl, Sl)
