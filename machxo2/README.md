# `nextpnr-machxo2`

_Experimental_ FOSS Place And Route backend for the Lattice MachXO2 family of
FPGAs. Fuzzing takes place as a subproject of [`prjtrellis`](https://github.com/YosysHQ/prjtrellis).

Known to work:

* Basic routing from pads to SLICEs and back!
* Basic packing of one type of FF and LUT into _half_ of a SLICE!
* Using the internal oscillator `OSCH` as a clock
* `LOGIC` SLICE mode

Things that probably work but are untested:

* Any non-3.3V I/O standard that doesn't use bank VREFs.

Things remaining to do include (but not limited to):

* More intelligent and efficient packing
* Global Routing (exists in database/sim models, `nextpnr-machxo2` doesn't use
  it yet)
* Secondary High Fanout Nets
* Edge Clocks (clock pads work, but not routed to global routing yet)
* PLLs
* Synchronous Release Global Set/Reset Interface (`SGSR`)
* Embedded Function Block (`EFB`)
* All DDR-related functionality
* Bank VREFs
* Embedded Block RAM (`EBR`)
* `CCU2` and `DPRAM` SLICE modes

## Quick Start

The following commands are known to work on a near-fresh Linux Mint system
(thank you [securelyfitz](https://twitter.com/securelyfitz)!):

### Prerequisites

```
sudo apt install cmake clang-format libboost-all-dev build-essential
qt5-default libeigen3-dev build-essential clang bison flex libreadline-dev
gawk tcl-dev libffi-dev git graphviz xdot pkg-config python3
libboost-system-dev libboost-python-dev libboost-filesystem-dev zlib1g-dev
python3-setuptools python3-serial
```

### Installation

Use an empty directory to hold all the cloned repositories. Upstream repos
can be used as well (e.g. [`YosysHQ/prjtrellis`](https://github.com/YosysHQ/prjtrellis),
etc.).

```
git clone git@github.com:cr1901/prjtrellis.git
cd prjtrellis
git checkout facade
git submodule update --init --recursive
cd libtrellis
cmake -DCMAKE_INSTALL_PREFIX=/usr
make -j 8
sudo make install

cd ../../

git clone git@github.com:cr1901/yosys.git
cd yosys/
git checkout machxo2
make config-gcc
make
sudo make install

cd ../

git clone git@github.com:tinyfpga/TinyFPGA-A-Programmer.git
cd TinyFPGA-A-Programmer/
sudo python setup.py install

cd ../

git clone git@github.com:cr1901/nextpnr.git
cd nextpnr
git checkout machxo2
git submodule update --init --recursive
cmake . -DARCH=machxo2 -DBUILD_GUI=OFF  -DTRELLIS_INSTALL_PREFIX=/usr -DBUILD_PYTHON=OFF 
make
```

Although uncommon, the `facade` and `machxo2` branches of the above repos are
occassionally rebased; use `git pull -f` if necessary.

### Demo

If you have a [TinyFPGA Ax2](https://store.tinyfpga.com/products/tinyfpga-a2) board
with the [TinyFPGA Programmer](https://store.tinyfpga.com/products/tinyfpga-programmer),
the following script will build a blinky bitstream and load it onto the
MachXO2; the gateware will flash the LED!

```
cd machxo2/examples/
sh demo.sh tinyfpga
```

The `tinyfpga.v` code used in `demo.sh` is slightly modified from the
[user's guide](https://tinyfpga.com/a-series-guide.html) to accommodate
`(* LOC = "pin" *)` constraints and the built-in user LED.
