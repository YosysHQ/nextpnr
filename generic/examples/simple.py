X = 12
Y = 12

def is_io(x, y):
	return x == 0 or x == X-1 or y == 0 or y == Y-1


for x in range(X):
	for y in range(Y):
		if is_io(x, y):
			if x == y:
				continue
			for z in range(2):
				ctx.addBel(name="X%dY%d_IO%d" % (x, y, z), type="GENERIC_IOB", loc=Loc(x, y, z), gb=False)
		else:
			ctx.addBel(name="X%dY%d_SLICE%d" % (x, y, z), type="GENERIC_SLICE", loc=Loc(x, y, z), gb=False)
