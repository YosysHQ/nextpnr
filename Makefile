archs = dummy
common_objs = design.o
dummy_objs = chip.o main.o

all::
clean::

include ice40/makefile.inc

CXX = clang
CXXFLAGS = -ggdb -MD -std=c++11 -O2 -Icommon
LDFLAGS = -ggdb
LDLIBS = -lstdc++

define binaries
all:: nextpnr-$(1)

nextpnr-$(1): $$(addprefix objs/$(1)-common-,$$(common_objs)) $$(addprefix objs/$(1)-arch-,$$($(1)_objs))
	$$(CXX) -o $$@ $$(LDFLAGS) -I$(1) $$^ $$(LDLIBS)

objs/$(1)-common-%.o: common/%.cc
	@mkdir -p objs
	$$(CXX) -c -o $$@ -D$$(shell echo arch_$(1) | tr a-z A-Z) $$(CXXFLAGS) -I$(1) $$<

objs/$(1)-arch-%.o: $(1)/%.cc
	@mkdir -p objs
	$$(CXX) -c -o $$@ -D$$(shell echo arch_$(1) | tr a-z A-Z) $$(CXXFLAGS) -I$(1) $$<
endef

$(foreach arch,$(archs),$(eval $(call binaries,$(arch))))

clean::
	rm -rf $(addprefix nextpnr-,$(archs)) objs

-include objs/*.d
