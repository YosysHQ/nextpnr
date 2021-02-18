## FPGA interchange nextpnr architecture

This nextpnr architecture is a meta architecture that in theory will implement
any architecture that emits a complete FPGA interchange device database.

### FPGA interchange

The FPGA interchange is a set of file formats intended to describe any modern
island based FPGA.  It consists of three primary file formats:

 - Device database
   - This is a description of a particular FPGA fabric.  This description
     includes placement locations, placement constraints and a complete
     description of the routing fabric.
   - This file will also include timing information once added.

 - Logical netlist
   - This is the output of a synthesis tool.  This is equivalent to the
     Yosys JSON format, EDIF, or eblif.
   - As part of future nextpnr development, a frontend will be added that
     takes this format as input.

 - Physical netlist
   - This is the output of a place and route tool. It can describe a clustered
     design, a partially or fully placed design, and a partially or fully
     routed design.

### Current status

This architecture implementation can be compiled in conjunction with a FPGA
interchange device database, and the outputs from
`fpga_interchange.nextpnr_emit`, which is part of the
[python-fpga-interchange](https://github.com/SymbiFlow/python-fpga-interchange/)
library.

The current implementation is missing essential features for place and route.
As these features are added, this implementation will become more useful.

 - [ ] Logical netlist macro expansion is not implemented, meaning that any
       macro primitives are unplaceable.  Common macro primitives examples are
       differential IO buffers (IBUFDS) and some LUT RAM (e.g. RAM64X1D).
 - [ ] The router lookahead is missing, meaning that router runtime
       performance will be terrible.
 - [ ] The routing graph that is currently emitted does not have ground and
       VCC networks, so all signals must currently be tied to an IO signal.
       Site pins being tied to constants also needs handling so that site
       local inverters are used rather than routing signals suboptimally.
 - [ ] Pseudo pips (e.g. pips that consume BELs and or site resources) should
       block their respective resources.  This effects designs that have some
       routing in place before placement.
 - [ ] Pseudo site pips (e.g. site pips that route through BELs) should block
       their respective resources. Without this, using some pseudo site pips
       could result in invalid placements.
 - [ ] Timing information is missing from the FPGA interchange device
       database, so it is also currently missing from the FPGA interchange
       architecture.  Once timing information is added to the device database
       schema, it needs to be added to the architecture.
 - [ ] Implemented site router lacks important features for tight packing,
       namely LUT rotation.  Also the current site router is relatively
       untested, so legal configurations may be rejected and illegal
       configurations may be accepted.

#### FPGA interchange fabrics

Currently only Xilinx 7-series, UltraScale and UltraScale+ fabrics have a
device database generator, via [RapidWright](https://github.com/Xilinx/RapidWright).

##### Artix 35T example

Install capnproto if not already installed:
```
# Or equivalent for your local system.
sudo apt-get install capnproto libcapnp-dev
```

Install capnproto-java if not already installed:
```
git clone https://github.com/capnproto/capnproto-java.git
cd capnproto-java
make
sudo make install
```

##### Makefile-driven BBA creation

In `${NEXTPNR_DIR}/fpga_interchange/examples/create_bba` is a Makefile that
should compile nextpnr and create a Xilinx A35 chipdb if java, capnproto and
capnproto-java are installed.

Instructions:
```
cd ${NEXTPNR_DIR}/fpga_interchange/examples/create_bba
make
```

This will create a virtual env in
`${NEXTPNR_DIR}/fpga_interchange/examples/create_bba/build/env` that has the
python-fpga-interchange library installed.  Before running the design examples,
enter the virtual env, e.g.:

```
source ${NEXTPNR_DIR}/fpga_interchange/examples/create_bba/build/env/bin/activate
```

The chipdb will be written to `${NEXTPNR_DIR}/fpga_interchange/examples/create_bba/build/xc7a35.bin`
once completed.

##### Manual BBA creation

This covers the manual set of steps to create a Xilinx A35 chipdb.

Download RapidWright and generate the device database.
```
# FIXME: Use main branch once interchange branch is merged.
git clone -b interchange https://github.com/Xilinx/RapidWright.git
cd RapidWright
make update_jars

# FIXME: Current RapidWright jars generate database with duplicate PIPs
# https://github.com/Xilinx/RapidWright/issues/127
# Remove this wget once the latest RapidWright JAR is published.
wget https://github.com/Xilinx/RapidWright/releases/download/v2020.2.1-beta/rapidwright-api-lib-2020.2.1_update1.jar
mv rapidwright-api-lib-2020.2.1_update1.jar jars/rapidwright-api-lib-2020.2.0.jar

./scripts/invoke_rapidwright.sh com.xilinx.rapidwright.interchange.DeviceResourcesExample xc7a35tcpg236-1
export RAPIDWRIGHT_PATH=$(pwd)
```

Set `INTERCHANGE_DIR` to point to 3rdparty/fpga-interchange-schema:
```
export INTERCHANGE_DIR=$(NEXTPNR_DIR)/3rdparty/fpga-interchange-schema/interchange
```

Install python FPGA interchange library.
```
git clone https://github.com/SymbiFlow/python-fpga-interchange.git
cd python-fpga-interchange
pip install -r requirements.txt
```

Patch device database with cell constraints and LUT annotations:
```
python3 -mfpga_interchange.patch \
  --schema_dir ${INTERCHANGE_DIR} \
  --schema device \
  --patch_path constraints \
  --patch_format yaml \
  ${RAPIDWRIGHT_PATH}/xc7a35tcpg236-1.device \
  test_data/series7_constraints.yaml \
  xc7a35tcpg236-1_constraints.device
python3 -mfpga_interchange.patch \
  --schema_dir ${INTERCHANGE_DIR} \
  --schema device \
  --patch_path lutDefinitions \
  --patch_format yaml \
  xc7a35tcpg236-1_constraints.device \
  test_data/series7_luts.yaml \
  xc7a35tcpg236-1_constraints_luts.device
```

Generate nextpnr BBA and constids.inc from device database:
```
python3 -mfpga_interchange.nextpnr_emit \
    --schema_dir ${INTERCHANGE_DIR} \
    --output_dir ${NEXTPNR_DIR}/fpga_interchange/ \
    --bel_bucket_seeds test_data/series7_bel_buckets.yaml \
    --device xc7a35tcpg236-1_constraints_luts.device \
```

Build nextpnr:

```
cd ${NEXTPNR_DIR}
cmake -DARCH=fpga_interchange .
make -j
```

Compile generated BBA:
```
bba/bbasm -l fpga_interchange/chipdb.bba fpga_interchange/chipdb.bin
```

Run nextpnr archcheck:
```
./nextpnr-fpga_interchange --chipdb fpga_interchange/chipdb.bin --test
```

Once nextpnr can complete the place and route task and output the physical
netlist, RapidWright can be used to generate a DCP suitable for bitstream
output and DRC checks.

```
${RAPIDWRIGHT_PATH}/scripts/invoke_rapidwright.sh \
    com.xilinx.rapidwright.interchange.PhysicalNetlistToDcp \
    <logical netlist file> <physical netlist file> <XDC file> <output DCP>
```
