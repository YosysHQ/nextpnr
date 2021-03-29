## FPGA interchange instructions

These are instructions on how to get the dependencies, generate the FPGA interchange architecture build system and
run some example designs.


### Installing dependencies

Install java and javac if not already installed:
```
# Or equivalent for your local system.
sudo apt-get install openjdk-10-jdk
```

Install capnproto if not already installed. Version 0.8.0 is required.
As stated in the [official instructions](https://capnproto.org/install.html), the version on the common package managers
might not be up to date with the latest version, hence it is suggested to install
from the archive or, in alternative, directly from the git repository.

Install capnproto-java if not already installed:
```
git clone https://github.com/capnproto/capnproto-java.git
cd capnproto-java
make
sudo make install
```

Install python-fpga-interchange if not already installed:
```
git clone https://github.com/SymbiFlow/python-fpga-interchange.git
cd python-fpga-interchange.git
#
# Note: Recommend checking out a specific release, for example:
#
#   git checkout v0.0.5
#
# Release of python-fpga-interchange library does have to match nextpnr
# implementation.
python -m pip install -e .
```

Clone RapidWright, if not already cloned:
```
git clone https://github.com/Xilinx/RapidWright.git
cd RapidWright
make update_jars
```

### Build instructions

Once dependencies are installed/cloned, configure the build system for the FPGA interchange.

From the nextpnr root dir run:

```
mkdir build
cd build
cmake .. --DARCH=fpga_interchange -DRAPIDWRIGHT_PATH=<RapidWright path> -DINTERCHANGE_SCHEMA_PATH=<fpga-interchange-schema path> -DPYTHON_INTERCHANGE_PATH=<python-fpga-interchange path>
```

To build the xc7a35t architecture, run:
```
make chipdb-xc7a35t-bin
```

To build the example designs run:
```
make test-fpga_interchange-wire_arty-dcp
```

The make targets for the example designs follow the same pattern: `test-fpga_interchange-<test_name>-<output>`, where `output` is the name of the intermediate step of the build which can be:

- `json`: synthesis output
- `netlist`: logical netlist
- `phys`: physical netlist
- `dcp`: design checkpoint
