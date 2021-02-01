# `nextpnr-machxo2`

To be filled in w/ details later.

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

Use an empty directory to hold all the cloned repositories.

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
cmake . -DARCH=machxo2 -DBUILD_GUI=OFF  -DTRELLIS_INSTALL_PREFIX=/usr -DBUILD_PYTHON=OFF -DBUILD_HEAP=OFF
make
```

### Demo

If you have a [TinyFPGA Ax2](https://store.tinyfpga.com/products/tinyfpga-a2) board
with the [TinyFPGA Programmer](https://store.tinyfpga.com/products/tinyfpga-programmer),
the following script will build a blinky bitstream and load it onto the
MachXO2; the gateware will flash the LED!

```
cd machxo2/examples/
sh demo.sh
```

The `tinyfpga.v` code used in `demo.sh` is slightly modified from the
[user's guide](https://tinyfpga.com/a-series-guide.html) to accommodate
`(* LOC = "pin" *)` constraints.
