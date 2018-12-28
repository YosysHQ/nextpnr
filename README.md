nextpnr -- a portable FPGA place and route tool
===============================================

***
### NB: This nextpnr-xc7 branch is *unofficial*, *very* proof-of-concept, *very* experimental, *very* unoptimised, and is provided with *no support whatsoever*. Use at your own risk!
#### It leverages a [torc](https://github.com/torc-isi/torc) fork with minimal changes (those necessary to support building on later versions of gcc) to target XDL-compatible devices.
### Note that torc is licensed under GPLv3 which differs from nextpnr's ISC license, thus please respect the limitations imposed by both licenses.
Currently, only LUT1-6, IOB, BUFGCTRL, MMCME2_ADV are supported for xc7z020 and xc7vx680t (but trivial to add others).
The following example shell scripts are available:
* blinky.sh -- generates blinky.bit that flashes (with a delay) the 4 LEDs on a ZYBO Z7
* blinky_sim.sh -- post place-and-route simulation, without any delays (requires [GHDL](https://github.com/ghdl/ghdl))
* picorv32.sh -- just places-and-routes picorv32.ncd (no testbench)
* attosoc.sh -- generates attosoc.bit of a self-stimulating picorv32 device that displays (with a delay) prime numbers to the LEDs -- when testing on hardware, consider using a PLL (MMCM) to meet timing
* attosoc_sim.sh -- post place-and-route simulation of a self-stimulating picorv32 device, without any delays (requires [GHDL](https://github.com/ghdl/ghdl))
***

nextpnr aims to be a vendor neutral, timing driven, FOSS FPGA place and route
tool.

Currently nextpnr supports:
 * Lattice iCE40 devices supported by [Project IceStorm](http://www.clifford.at/icestorm/)
 * *(experimental)* Lattice ECP5 devices supported by [Project Trellis](https://github.com/SymbiFlow/prjtrellis)
 * *(experimental)* a "generic" back-end for user-defined architectures

We hope to see Xilinx 7 Series thanks to
[Project X-Ray](https://github.com/SymbiFlow/prjxray) and even more FPGA families
supported in the future. We would love your help in developing this
awesome new project!

Here is a screenshot of nextpnr for iCE40. Build instructions and
[getting started notes](#getting-started) can be found below.

<img src="https://i.imgur.com/0spmlBa.png" width="640"/>

See also:
- [F.A.Q.](docs/faq.md)
- [Architecture API](docs/archapi.md)


Prerequisites
-------------

The following packages need to be installed for building nextpnr, independent
of the selected architecture:

- CMake 3.3 or later
- Modern C++11 compiler (`clang-format` required for development)
- Qt5 or later (`qt5-default` for Ubuntu 16.04)
- Python 3.5 or later, including development libraries (`python3-dev` for Ubuntu)
  - on Windows make sure to install same version as supported by [vcpkg](https://github.com/Microsoft/vcpkg/blob/master/ports/python3/CONTROL)
- Boost libraries (`libboost-dev libboost-filesystem-dev libboost-thread-dev libboost-program-options-dev libboost-python-dev libboost-dev` or `libboost-all-dev` for Ubuntu)
- Latest git Yosys is required to synthesise the demo design
- For building on Windows with MSVC, usage of vcpkg is advised for dependency installation.
  - For 32 bit builds: `vcpkg install boost-filesystem boost-program-options boost-thread boost-python qt5-base`
  - For 64 bit builds: `vcpkg install boost-filesystem:x64-windows boost-program-options:x64-windows boost-thread:x64-windows boost-python:x64-windows qt5-base:x64-windows`
- For building on macOS, brew utility is needed.
  - Install all needed packages `brew install cmake python boost boost-python3 qt5`
  - Do not forget to add qt5 in path as well `echo 'export PATH="/usr/local/opt/qt/bin:$PATH"' >> ~/.bash_profile`

Getting started
---------------

### nextpnr-ice40

To build the iCE40 version of nextpnr, install [icestorm](http://www.clifford.at/icestorm/) with chipdbs installed in `/usr/local/share/icebox`,
or another location, which should be passed as `-DICEBOX_ROOT=/path/to/share/icebox` (ensure to point it to `share/icebox` and not where the
icebox binaries are installed) to CMake.
Then build and install `nextpnr-ice40` using the following commands:

```
cmake -DARCH=ice40 .
make -j$(nproc)
sudo make install
```

A simple example that runs on the iCEstick dev board can be found in `ice40/blinky.*`.
Usage example:

```
cd ice40
yosys -p 'synth_ice40 -top blinky -json blinky.json' blinky.v               # synthesize into blinky.json
nextpnr-ice40 --hx1k --json blinky.json --pcf blinky.pcf --asc blinky.asc   # run place and route
icepack blinky.asc blinky.bin                                               # generate binary bitstream file
iceprog blinky.bin                                                          # upload design to iCEstick
```

Running nextpnr in GUI mode:

```
nextpnr-ice40 --json blinky.json --pcf blinky.pcf --asc blinky.asc --gui
```

(Use the toolbar buttons or the Python command console to perform actions
such as pack, place, route, and write output files.)

### nextpnr-ecp5

For ECP5 support, you must download [Project Trellis](https://github.com/SymbiFlow/prjtrellis),
then follow its instructions to download the latest database and build _libtrellis_.

```
cmake -DARCH=ecp5 -DTRELLIS_ROOT=/path/to/prjtrellis .
make -j$(nproc)
sudo make install
```

 - For an ECP5 blinky on the 45k ULX3S board, first synthesise using `yosys blinky.ys` in `ecp5/synth`.
  - Then run ECP5 place-and route using `./nextpnr-ecp5 --json ecp5/synth/blinky.json --basecfg ecp5/synth/ulx3s_empty.config --textcfg ecp5/synth/ulx3s_out.config`
  - Create a bitstream using `ecppack ulx3s_out.config ulx3s.bit`
  - Note that `ulx3s_empty.config` contains fixed/unknown bits to be copied to the output bitstream

 - More examples of the ECP5 flow for a range of boards can be found in the [Project Trellis Examples](https://github.com/SymbiFlow/prjtrellis/tree/master/examples).


### nextpnr-generic

The generic target allows running placement and routing for arbitrary custom architectures.

```
cmake -DARCH=generic .
make -j$(nproc)
sudo make install
```

TBD: Getting started example for generic target.

Additional notes for building nextpnr
-------------------------------------

Use cmake `-D` options to specify which version of nextpnr you want to build.

Use `-DARCH=...` to set the architecture. It is a semicolon separated list.
Use `cmake . -DARCH=all` to build all supported architectures.

The following runs a debug build of the iCE40 architecture without GUI
and without Python support and only HX1K support:

```
cmake -DARCH=ice40 -DCMAKE_BUILD_TYPE=Debug -DBUILD_PYTHON=OFF -DBUILD_GUI=OFF -DICE40_HX1K_ONLY=1 .
make -j$(nproc)
```

To make static build relase for iCE40 architecture use the following:

```
cmake -DARCH=ice40 -DBUILD_PYTHON=OFF -DBUILD_GUI=OFF -DSTATIC_BUILD=ON .
make -j$(nproc)
```

You can change the location where nextpnr will be installed (this will usually default to `/usr/local`) by using
`-DCMAKE_INSTALL_PREFIX=/install/prefix`.

Notes for developers
--------------------

- All code is formatted using `clang-format` according to the style rules in `.clang-format` (LLVM based with
  increased indent widths and brace wraps after classes).
- To automatically format all source code, run `make clangformat`.
- See the wiki for additional documentation on the architecture API.

Testing
-------

- To build test binaries as well, use `-DBUILD_TESTS=ON` and after `make` run `make tests` to run them, or you can run separate binaries.
- To use code sanitizers use the `cmake` options:
  - `-DSANITIZE_ADDRESS=ON`
  - `-DSANITIZE_MEMORY=ON -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++`
  - `-DSANITIZE_THREAD=ON`
  - `-DSANITIZE_UNDEFINED=ON`
- Running valgrind example `valgrind --leak-check=yes --tool=memcheck ./nextpnr-ice40 --json ice40/blinky.json`
- Running tests with code coverage use `-DBUILD_TESTS=ON -DCOVERAGE` and after `make` run `make ice40-coverage` 
- After that open `ice40-coverage/index.html` in your browser to view the coverage report
- Note that `lcov` is needed in order to generate reports

Links and references
--------------------

### Synthesis, simulation, and logic optimization

- [Yosys](http://www.clifford.at/yosys/)
- [Icarus Verilog](http://iverilog.icarus.com/)
- [ABC](https://people.eecs.berkeley.edu/~alanmi/abc/)

### FPGA bitstream documentation (and tools) projects

- [Project IceStorm (Lattice iCE40)](http://www.clifford.at/icestorm/)
- [Project Trellis (Lattice ECP5)](https://symbiflow.github.io/prjtrellis-db/)
- [Project X-Ray (Xilinx 7-Series)](https://symbiflow.github.io/prjxray-db/)
- [Project Chibi (Intel MAX-V)](https://github.com/rqou/project-chibi)

### Other FOSS FPGA place and route projects

- [Arachne PNR](https://github.com/cseed/arachne-pnr)
- [VPR/VTR](https://verilogtorouting.org/)
- [SymbiFlow](https://github.com/SymbiFlow/symbiflow-arch-defs)
- [Gaffe](https://github.com/gaffe-logic/gaffe)
- [KinglerPAR](https://github.com/rqou/KinglerPAR)

