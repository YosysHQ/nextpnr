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
(python-fpga-interchange)[https://github.com/SymbiFlow/python-fpga-interchange/]
library.

The current implementation is missing essential features for place and route.
As these features are added, this implementation will become more useful.

 - [ ] Placement constraints are unimplemented, meaning invalid or unroutable
       designs can be generated from the placer.
 - [ ] Logical netlist macro expansion is not implemented, meaning that any
       macro primitives are unplacable.  Common macro primitives examples are
       differential IO buffers (IBUFDS) and some LUT RAM (e.g. RAM64X1D).
 - [ ] Cell -> BEL pin mapping is not in place, meaning any primitives that
       have different BEL pins with respect to their cell pins will not be
       routable.
 - [ ] Nextpnr only allows for cell -> BEL pin maps that are 1 to 1.  The
       FPGA interchange accomidates cell -> BEL pin maps that include 1 to
       many relationship for sinks.  A common primitives that uses 1 to many
       maps are the RAMB18E1.
 - [ ] The router lookahead is missing, meaning that router runtime
       performance will be terrible.
 - [ ] Physical netlist backend is missing, so even if
       `nextpnr-fpga_interchange` completes successfully, there is no way to
       generate output that can be consumed by downstream tools.
 - [ ] XDC parsing and port constraints are unimplemented, so IO pins cannot
       be fixed.  The chipdb BBA output is also missing package pin data, so
       only site constraints are currently possible. Eventually the chipdb BBA
       should also include package pin data to allow for ports to be bound to
       package pins.
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

#### FPGA interchange fabrics

Currently only Xilinx 7-series, UltraScale and UltraScale+ fabrics have a
device database generator, via (RapidWright)[https://github.com/Xilinx/RapidWright].

##### Artix 35T example

Download RapidWright and generate the device database.
```
# FIXME: Use main branch once interchange branch is merged.
git clone -b interchange https://github.com/Xilinx/RapidWright.git
cd RapidWright
make update_jars

# FIXME: Current RapidWright jars generate database with duplicate PIPs
# https://github.com/Xilinx/RapidWright/issues/127
# Remove this wget once latest RapidWright JAR is published.
wget https://github.com/Xilinx/RapidWright/releases/download/v2020.2.1-beta/rapidwright-api-lib-2020.2.1_update1.jar
mv rapidwright-api-lib-2020.2.1_update1.jar jars/rapidwright-api-lib-2020.2.0.jar

./scripts/invoke_rapidwright.sh com.xilinx.rapidwright.interchange.DeviceResourcesExample xc7a35tcpg236-1
export RAPIDWRIGHT_PATH=$(pwd)
export INTERCHANGE_DIR=$(pwd)/interchange
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
python3 -mfpga_interchange.patch \
  --schema_dir ${RAPIDWRIGHT_PATH}/interchange/ \
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
    --device xc7a35tcpg236-1_constraints_luts.device
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
