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
		--run $(NEXTPNR_PATH)/python/check_arch_api.py
