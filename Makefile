archs = dummy
common_objs = design.o
dummy_objs = chip.o main.o

CXX = clang
CXXFLAGS = -ggdb -MD -std=c++11 -O2 -Icommon
LDFLAGS = -ggdb
LDLIBS = -lstdc++

define binaries
all:: nextpnr-$(1)

nextpnr-$(1): $$(addprefix build/$(1)-common-,$$(common_objs)) $$(addprefix build/$(1)-arch-,$$($(1)_objs))
	$$(CXX) -o $$@ $$(LDFLAGS) -I$(1) $$^ $$(LDLIBS)

build/$(1)-common-%.o: common/%.cc
	@mkdir -p build
	$$(CXX) -c -o $$@ -D$$(shell echo arch_$(1) | tr a-z A-Z) $$(CXXFLAGS) -I$(1) $$<

build/$(1)-arch-%.o: $(1)/%.cc
	@mkdir -p build
	$$(CXX) -c -o $$@ -D$$(shell echo arch_$(1) | tr a-z A-Z) $$(CXXFLAGS) -I$(1) $$<
endef

$(foreach arch,$(archs),$(eval $(call binaries,$(arch))))

clean:
	rm -rf $(addprefix nextpnr-,$(archs)) build

-include build/*.d
