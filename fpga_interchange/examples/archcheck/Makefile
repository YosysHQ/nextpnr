include ../common.mk

PACKAGE := csg324

.PHONY: check check_test_data

check: check_test_data
	$(NEXTPNR_BIN) \
		--chipdb $(BBA_PATH) \
		--package $(PACKAGE) \
		--test

check_test_data:
	$(NEXTPNR_BIN) \
		--chipdb $(BBA_PATH) \
		--package $(PACKAGE) \
		--run $(NEXTPNR_PATH)/python/check_arch_api.py

debug_check_test_data:
	gdb --args $(NEXTPNR_BIN) \
		--chipdb $(BBA_PATH) \
		--package $(PACKAGE) \
		--run $(NEXTPNR_PATH)/python/check_arch_api.py
