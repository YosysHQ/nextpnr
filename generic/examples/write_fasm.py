from collections import namedtuple

"""
	write:   set to True to enable writing this parameter to FASM

	numeric: set to True to write this parameter as a bit array (width>1) or
	         single bit (width==1) named after the parameter. Otherwise this
	         parameter will be written as `name.value`

	width:   width of numeric parameter (ignored for non-numeric parameters)
	
	alias:   an alternative name for this parameter (parameter name used if alias
	         is None)
"""
ParameterConfig = namedtuple('ParameterConfig', 'write numeric width alias')

# FIXME use defaults= once Python 3.7 is standard
ParameterConfig.__new__.__defaults__ = (False, True, 1, None)


"""
Write a design as FASM

    ctx:      nextpnr context
    paramCfg: map from (celltype, parametername) -> ParameterConfig describing how to write parameters
    f:        output file
"""
def write_fasm(ctx, paramCfg, f):
	for nname, net in sorted(ctx.nets, key=lambda x: str(x[1].name)):
		print("# Net %s" % nname, file=f)
		for wire, pip in sorted(net.wires, key=lambda x: str(x[1])):
			if pip.pip != "":
				print("%s" % pip.pip, file=f)
		print("", file=f)
	for cname, cell in sorted(ctx.cells, key=lambda x: str(x[1].name)):
		print("# Cell %s at %s" % (cname, cell.bel), file=f)
		for param, val in sorted(cell.params, key=lambda x: str(x)):
			cfg = paramCfg[(cell.type, param)]
			if not cfg.write:
				continue
			fasm_name = cfg.alias if cfg.alias is not None else param
			if cfg.numeric:
				if cfg.width == 1:
					if int(val) != 0:
						print("%s.%s" % (cell.bel, fasm_name), file=f)
				else:
					# Parameters with width >32 are direct binary, otherwise denary
					print("%s.%s[%d:0] = %d'b%s" % (cell.bel, fasm_name, cfg.width-1, cfg.width, val), file=f)
			else:
				print("%s.%s.%s" % (cell.bel, fasm_name, val), file=f)
		print("", file=f)