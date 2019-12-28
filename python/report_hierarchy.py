def visit(indent, data):
	istr = " " * indent
	print("{}{}: {}".format(istr, data.name, data.type))
	for lname, gname in data.leaf_cells:
		print("{}    {} -> {}".format(istr, lname, gname))
	for lname, gname in data.hier_cells:
		visit(indent + 4, ctx.hierarchy[gname])

visit(0, ctx.hierarchy[ctx.top_module])

