NEXTPNR_PATH := $(realpath ../../..)
NEXTPNR_BIN := $(NEXTPNR_PATH)/build/nextpnr-fpga_interchange
BBA_PATH := $(realpath ..)/create_bba/build/xc7a35.bin

RAPIDWRIGHT_PATH := $(realpath ..)/create_bba/build/RapidWright
INTERCHANGE_PATH := $(realpath ..)/create_bba/build/fpga-interchange-schema/interchange

DEVICE := $(realpath ..)/create_bba/build/python-fpga-interchange/xc7a35tcpg236-1_constraints_luts.device
