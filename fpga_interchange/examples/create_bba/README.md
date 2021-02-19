## Makefile-driven BBA creation

This Makefile will generate a Xilinx A35 chipdb if java, capnproto and
capnproto-java are installed.

### Installing dependencies

Install java and javac if not already installed:
```
# Or equivalent for your local system.
sudo apt-get install openjdk-10-jdk
```

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

### Instructions

Once dependencies are installed, just run "make".  This should download
remaining dependencies and build the chipdb and build nextpnr if not built.

#### Re-building the chipdb

```
# Remove the text BBA
rm build/nextpnr/fpga_interchange/chipdb.bba
# Build the BBA
make
```
