ctx.createRectangularRegion("osc", 1, 1, 1, 4)
for cell, cellinfo in ctx.cells:
    if "ringosc" in cellinfo.attrs:
        print("Floorplanned cell %s" % cell)
        ctx.constrainCellToRegion(cell, "osc")
