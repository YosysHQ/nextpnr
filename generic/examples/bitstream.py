from write_fasm import *
from simple_config import K

# Need to tell FASM generator how to write parameters
# (celltype, parameter) -> ParameterConfig
param_map = {
	("GENERIC_SLICE", "K"): ParameterConfig(write=False),
	("GENERIC_SLICE", "INIT"): ParameterConfig(write=True, numeric=True, width=2**K),
	("GENERIC_SLICE", "FF_USED"): ParameterConfig(write=True, numeric=True, width=1),

	("GENERIC_IOB", "INPUT_USED"): ParameterConfig(write=True, numeric=True, width=1),
	("GENERIC_IOB", "OUTPUT_USED"): ParameterConfig(write=True, numeric=True, width=1),
	("GENERIC_IOB", "ENABLE_USED"): ParameterConfig(write=True, numeric=True, width=1),
}

with open("blinky.fasm", "w") as f:
	write_fasm(ctx, param_map, f)
