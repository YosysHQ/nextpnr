nextpnr -- a portable FPGA place and route tool
===============================================

nextpnr aims to be a vendor neutral, timing driven, FOSS FPGA place and route
tool.

nextpnr was [started by SymbioticEDA](https://www.symbioticeda.com/) as an
experimental replacement for the existing Arachne-PNR base toolchain (hence the
name *next*pnr). Focusing on supporting timing driven place and route for
multiple FPGA architectures from the ground up, the experiment has proven
successful and we believe nextpnr is on its way to being a suitable
replacement for all users of Arachne-PNR.

Currently nextpnr supports;
 * Lattice iCE40 devices supported by [Project IceStorm](http://www.clifford.at/icestorm/),
 * *(experimental)* Lattice ECP5 devices supported by [Project Trellis](https://github.com/SymbiFlow/prjtrellis),
 * *(experimental)* a "generic" back-end for user-defined architectures

We hope to see Xilinx 7 Series thanks to
[Project X-Ray](https://github.com/SymbiFlow/prjxray) and even more vendor's
FPGAs supported in the future. We would love your help in developing this
awesome new project.

Here is a screenshot of nextpnr for iCE40. Build instructions and
[getting started notes](#getting-started) and an [FAQ](#FAQ) can be found
below.

<img src="https://i.imgur.com/0spmlBa.png" width="640"/>


Prerequisites
-------------

The following packages need to be installed for building nextpnr, independent
of the selected architecture:

- CMake 3.3 or later
- Modern C++11 compiler (`clang-format` required for development)
- Qt5 or later (`qt5-default` for Ubuntu 16.04)
- Python 3.5 or later, including development libraries (`python3-dev` for Ubuntu)
  - on Windows make sure to install same version as supported by [vcpkg](https://github.com/Microsoft/vcpkg/blob/master/ports/python3/CONTROL)
- Boost libraries (`libboost-dev` or `libboost-all-dev` for Ubuntu)
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

To build the iCE40 version of nextpnr, install [icestorm](http://www.clifford.at/icestorm/) with chipdbs installed in `/usr/local/share/icebox`.
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
cmake -DARCH=ecp5 .
make -j$(nproc)
sudo make install
```

 - For an ECP5 blinky, first synthesise using `yosys blinky.ys` in `ecp5/synth`.
  - Then run ECP5 place-and route using `./nextpnr-ecp5 --json ecp5/synth/blinky.json --basecfg ecp5/synth/ulx3s_empty.config --bit ecp5/synth/ulx3s.bit`
  - Note that `ulx3s_empty.config` contains fixed/unknown bits to be copied to the output bitstream
  - You can also use `--textcfg out.config` to write a text file describing the bitstream for debugging

 - More examples of the ECP5 flow for a range of boards can be found in the [Project Trellis Examples](https://github.com/SymbiFlow/prjtrellis/tree/master/examples).

 - Currently the ECP5 flow supports LUTs, flipflops and IO. IO must be instantiated using `TRELLIS_IO` primitives and constraints specified
   using `LOC` and `IO_TYPE` attributes on those instances, as is used in the examples.

### nextpnr-generic

The generic target allows to run place and route for an arbitrary custom architecture.

```
cmake -DARCH=generic .
make -j$(nproc)
sudo make install
```

TBD: Getting started example for generic target.

Additional notes for building nextpnr
-------------------------------------

Use cmake `-D` options to specify which version of nextpnr you want to build.

Use `-DARCH=...` to set the architecture. It is semicolon separated list.
Use `cmake . -DARCH=all` to build all supported architectures.

The following runs a debug build of the iCE40 architecture without GUI
and without Python support and only HX1K support:

```
cmake -DARCH=ice40 -DCMAKE_BUILD_TYPE=Debug -DBUILD_PYTHON=OFF -DBUILD_GUI=OFF -DICE40_HX1K_ONLY=1 .
make -j$(nproc)
```

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


FAQ
---

### Which tool chain should I use and why?

 * If you wish to do new **research** into FPGA architectures, place and route
   algorithms or other similar topics, we suggest you look at using
   [Verilog to Routing](https://verilogtorouting.org).

 * If you are developing FPGA code in **Verilog** for a **Lattice iCE40** and
   need an open source toolchain, we suggest you use nextpnr.

 * If you are developing FPGA code in **Verilog** for a **Lattice iCE40** with
   the **existing Arachne-PNR toolchain**, we suggest you start thinking about
   migrating to nextpnr.

 * If you are developing Verilog FPGA code targeted at the Lattice ECP5 and
   need an open source toolchain, you may consider the **extremely
   experimental** ECP5 support in nextpnr.

 * If you are developing FPGA code in **VHDL** you will need to use either a
   version of [Yosys with Verific support]() or the vendor provided tools due
   to the lack of open source VHDL support in Yosys.

### Why didn't you just improve [Arachne-PNR](https://github.com/cseed/arachne-pnr)?

[Arachne-PNR](https://github.com/cseed/arachne-pnr) was originally developed as
part of [Project IceStorm](http://www.clifford.at/icestorm/) to demonstrate it
was possible to create an open source place and route tool for the iCE40 FPGAs
that actually produced valid bitstreams.

For it's original purpose it has served the community extremely well. However,
it was never designed to support multiple different FPGA devices, nor more
complicated timing driven routing used by most commercial place and route
tools.

It felt like extending Arachne-PNR was not going to be the best path forward, so
[SymbioticEDA](https://www.symbioticeda.com/) decided to invest in an
experiment around creating a replacement. nextpnr is the result of that
experiment and we believe well on it's way to being a direct replacement for
Arachne-PNR (and hence why it is called *next*pnr).

### Arachne-PNR does X better!

If you have a use case which prevents you from switching to nextpnr from
Arachne, we want to hear about it! Please create an issue following the
[Arachne-PNR regression template]() and we will do our best to solve the problem!

We want nextpnr to be a suitable replacement for anyone who is currently a user
of Arachne.

### Why are you not just contributing to [Verilog to Routing](https://verilogtorouting.org)?

We believe that [Verilog to Routing](https://verilogtorouting.org) is a great
tool and many of the nextpnr developers have made (and continue to make)
contributions to the project.

VtR is an extremely flexible tool but focuses on research around FPGA
architecture and algorithm development. If your goal is research, then we very
much encourage you to look into VtR further!

nextpnr takes a different approach by focusing on users developing FPGA code
for current FPGAs.

We also believe that support for real architectures will enable interesting new
research. nextpnr (like all place and route systems). depends heavily on
research groups like the VtR developers to investigate and push forward FPGA
algorithms in new and exciting ways.

#### What is VPR?

VPR is the "place and route" tool from Verilog To Routing. It has a similar
role in an FPGA development flow as nextpnr.

### What about [SymbiFlow](http://symbiflow.github.io)?

We expect that as nextpnr matures, it will become a key part of the
[SymbiFlow](http://github.com/SymbiFlow). For now, while still in a more
experimental state SymbioticEDA will continue to host the project.

For the moment SymbiFlow is continuing to concentrate on extending Verilog to
Routing tool to work with real world architectures.

### Who is working on this project?

nextpnr was
[started as an experiment by SymbioticEDA](https://www.symbioticeda.com/) but
hopes to grow beyond being both just an experiment and developed by a single
company. Like Linux grew from Linus Torvalds experiment in creating his own
operating system to something contributed too by many different companies, are
hope is the same will happen here.

The project has already accepted a number of contributions from people not
employed by SymbioticEDA and now with the public release encourages the
community to contribute too.


### What is [Project Trellis](https://github.com/SymbiFlow/prjtrellis)?

[Project Trellis](https://github.com/SymbiFlow/prjtrellis) is the effort to
document the bitstream format for the Lattice ECP5 series of FPGAs. It also
includes tooling around bitstream creation.

Project Trellis is used by nextpnr to enable support for creation of bitstreams
for these parts.

### What is [Project X-Ray](https://github.com/SymbiFlow/prjxray)?

[Project X-Ray](https://github.com/SymbiFlow/prjxray) is the effort to document
the bitstream format for the Xilinx Series 7 series of FPGAs. It also includes
tooling around bitstream generation for these parts.

While nextpnr currently does **not** support these Xilinx parts, we expect it
will soon by using Project X Ray in a similar manner to Project Trellis.

### What is [Project IceStorm](http://www.clifford.at/icestorm/)?

[Project IceStorm](http://www.clifford.at/icestorm/) was both a project to
document the bitstream for the Lattice iCE40 series of parts **and** a full
flow including Yosys and Arachne-PNR for converting Verilog into a bitstream for
these parts.

As the open source community now has support for multiple different FPGA parts,
in the nextpnr documentation we generally use Project IceStorm to mean the
tools that fulfil the same role as Project Trellis or Project X-Ray.

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
- [Gaffe](https://github.com/kc8apf/gaffe)
- [KinglerPAR](https://github.com/rqou/KinglerPAR)

