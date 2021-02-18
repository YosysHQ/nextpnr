#
#  nextpnr -- Next Generation Place and Route
#
#  Copyright (C) 2021  Symbiflow Authors
#
#
#  Permission to use, copy, modify, and/or distribute this software for any
#  purpose with or without fee is hereby granted, provided that the above
#  copyright notice and this permission notice appear in all copies.
#
#  THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
#  WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
#  MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
#  ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
#  WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
#  ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
#  OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.


# This Makefile provides a streamlined way to create an example
# FPGA interchange BBA suitable for placing and routing on Xilinx A35 parts.
#
# FPGA interchange device database is generated via RapidWright.
#
# Currently FPGA interchange physical netlist (e.g. place and route route) to
# FASM support is not done, so bitstream generation relies on RapidWright to
# convert FPGA interchange logical and physical netlist into a Vivado DCP.

include ../common.mk

.DELETE_ON_ERROR:

.PHONY: all chipdb test debug_test

all: chipdb

build:
	mkdir build

build/RapidWright: | build
	# FIXME: Update URL / branch as fixes are merged upstream and / or
	# interchange branch on Xilinx/RapidWright is merged to master branch.
	#
	#cd build && git clone -b interchange https://github.com/Xilinx/RapidWright.git
	cd build && git clone -b move_strlist https://github.com/litghost/RapidWright.git

build/env: | build
	python3 -mvenv build/env

build/python-fpga-interchange: | build
	cd build && git clone https://github.com/SymbiFlow/python-fpga-interchange.git

build/fpga-interchange-schema: | build
	cd build && git clone https://github.com/SymbiFlow/fpga-interchange-schema.git

build/.setup: | build/env build/fpga-interchange-schema build/python-fpga-interchange build/RapidWright
	source build/env/bin/activate && \
		cd build/python-fpga-interchange/ && \
		pip install -r requirements.txt
	touch build/.setup

$(NEXTPNR_PATH)/build:
	mkdir $(NEXTPNR_PATH)/build

$(NEXTPNR_PATH)/build/bba/bbasm: | $(NEXTPNR_PATH)/build
	cd $(NEXTPNR_PATH)/build && cmake -DARCH=fpga_interchange ..
	make -j -C $(NEXTPNR_PATH)/build

build/nextpnr/fpga_interchange/chipdb.bba: build/.setup
	mkdir -p build/nextpnr/fpga_interchange
	source build/env/bin/activate && \
		cd build/python-fpga-interchange/ && \
		make \
		-f Makefile.rapidwright \
		NEXTPNR_PATH=$(realpath .)/build/nextpnr \
		RAPIDWRIGHT_PATH=$(RAPIDWRIGHT_PATH) \
		INTERCHANGE_PATH=$(INTERCHANGE_PATH)

$(BBA_PATH): $(NEXTPNR_PATH)/build/bba/bbasm build/nextpnr/fpga_interchange/chipdb.bba
	$(NEXTPNR_PATH)/build/bba/bbasm -l build/nextpnr/fpga_interchange/chipdb.bba $(BBA_PATH)

chipdb: $(BBA_PATH)

test: chipdb
	$(NEXTPNR_PATH)/build/nextpnr-fpga_interchange \
		--chipdb $(BBA_PATH) \
		--package csg324 \
		--test

debug_test: chipdb
	gdb --args $(NEXTPNR_PATH)/build/nextpnr-fpga_interchange \
		--chipdb $(BBA_PATH) \
		--package csg324 \
		--test

clean:
	rm -rf build
