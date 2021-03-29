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

### Current development status

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
 - [ ] Timing information is missing from the FPGA interchange device
       database, so it is also currently missing from the FPGA interchange
       architecture.  Once timing information is added to the device database

#### Weaknesses of current implementation

Initial development on the following features is started, but needs more
refinement.

 - [ ] BEL validity checking is too expensive.  The majority of the runtime
       is currently in the LUT rotation.  Profiling, optimization and
       algorithm review is likely required to bring strict legalisation
       runtimes into expected levels.
 - [ ] The router lookahead is disabled by default.  Without the lookahead,
       router runtime is terrible.  However the current lookahead
       implementation is slow to compute and memory intensive, hence why it is
       disabled by default.
 - [ ] Pseudo pips (e.g. pips that consume BELs and or site resources) and
       pseudo site pips (e.g. site pips that route through BELs) consume site
       wires to indicate that they block some resources.  This covers many
       validity check cases, but misses some.  In particular, when a pseudo
       pip / pseudo site pip has an implication on the constraint system (e.g.
       LUT on a LUT-RAM BEL), an edge may be allowed incorrectly, resulting
       in an illegal design.

### FPGA interchange fabrics

Xilinx 7-series, UltraScale and UltraScale+ fabrics have a
device database generator, via [RapidWright](https://github.com/Xilinx/RapidWright).

A Lattice Nexus device database is being worked on, via
[prjoxide](https://github.com/gatecat/prjoxide).

### FPGA interchange build system

Construction of chipdb's is currently integrated into nextpnr's CMake build
system.  See fpga\_interchange/examples/README.md for more details.
