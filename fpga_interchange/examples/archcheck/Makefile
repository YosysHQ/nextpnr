NEXTPNR_PATH := $(shell echo ~/cat_x/nextpnr)
NEXTPNR_BIN := $(NEXTPNR_PATH)/build/nextpnr-fpga_interchange
BBA_PATH := $(NEXTPNR_PATH)/build/test.bin

PACKAGE := csg324

.PHONY:

check:
	$(NEXTPNR_BIN) \
		--chipdb $(BBA_PATH) \
		--package $(PACKAGE) \
		--test
	$(NEXTPNR_BIN) \
		--chipdb $(BBA_PATH) \
		--run $(NEXTPNR_PATH)/python/check_arch_api.py
