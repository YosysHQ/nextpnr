NEXTPNR_PATH := $(shell echo ~/cat_x/nextpnr)
NEXTPNR_BIN := $(NEXTPNR_PATH)/build/nextpnr-fpga_interchange
BBA_PATH := $(NEXTPNR_PATH)/build/test.bin

RAPIDWRIGHT_PATH := $(shell echo ~/cat_x/RapidWright)

INTERCHANGE_PATH := $(NEXTPNR_PATH)/3rdparty/fpga-interchange-schema/interchange

DEVICE := $(shell echo ~/cat_x/python-fpga-interchange/xc7a35tcpg236-1_constraints_luts.device)

.DELETE_ON_ERROR:
.PHONY: all debug clean

all: build/$(DESIGN).dcp

build:
	mkdir build

build/$(DESIGN).netlist: build/$(DESIGN).json
	/usr/bin/time -v python3 -mfpga_interchange.yosys_json \
		--schema_dir $(INTERCHANGE_PATH) \
		--device $(DEVICE) \
		--top $(DESIGN_TOP) \
		build/$(DESIGN).json \
		build/$(DESIGN).netlist

build/$(DESIGN)_netlist.yaml: build/$(DESIGN).netlist
	/usr/bin/time -v python3 -mfpga_interchange.convert \
		--schema_dir $(INTERCHANGE_PATH) \
		--schema logical \
		--input_format capnp \
		--output_format yaml \
		build/$(DESIGN).netlist \
		build/$(DESIGN)_netlist.yaml

build/$(DESIGN).phys: build/$(DESIGN).netlist
	$(NEXTPNR_BIN) \
		--chipdb $(BBA_PATH) \
		--xdc $(DESIGN).xdc \
		--netlist build/$(DESIGN).netlist \
		--phys build/$(DESIGN).phys \
		--package $(PACKAGE)

build/$(DESIGN)_phys.yaml: build/$(DESIGN).phys
	/usr/bin/time -v python3 -mfpga_interchange.convert \
		--schema_dir $(INTERCHANGE_PATH) \
		--schema physical \
		--input_format capnp \
		--output_format yaml \
		build/$(DESIGN).phys \
		build/$(DESIGN)_phys.yaml

debug: build/$(DESIGN).netlist
	gdb --args $(NEXTPNR_BIN) \
		--chipdb $(BBA_PATH) \
		--xdc $(DESIGN).xdc \
		--netlist build/$(DESIGN).netlist \
		--phys build/$(DESIGN).phys \
		--package $(PACKAGE)

build/$(DESIGN).dcp: build/$(DESIGN).netlist build/$(DESIGN).phys $(DESIGN).xdc
	RAPIDWRIGHT_PATH=$(RAPIDWRIGHT_PATH) \
		$(RAPIDWRIGHT_PATH)/scripts/invoke_rapidwright.sh \
		com.xilinx.rapidwright.interchange.PhysicalNetlistToDcp \
		build/$(DESIGN).netlist build/$(DESIGN).phys $(DESIGN).xdc build/$(DESIGN).dcp

clean::
	rm -rf build
