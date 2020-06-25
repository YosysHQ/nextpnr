nextpnr -- a portable FPGA place and route tool
===============================================

nextpnr aims to be a vendor neutral, timing driven, FOSS FPGA place and route
tool.

Currently nextpnr supports:
 * Lattice iCE40 devices supported by [Project IceStorm](http://www.clifford.at/icestorm/)
 * Lattice ECP5 devices supported by [Project Trellis](https://github.com/SymbiFlow/prjtrellis)
 * *(experimental)* a "generic" back-end for user-defined architectures

There is some work in progress towards [support for Xilinx devices](https://github.com/daveshah1/nextpnr-xilinx/) but it is not upstream and not intended for end users at the present time. We hope to see more FPGA families supported in the future. We would love your help in developing this awesome new project!

A brief (academic) paper describing the Yosys+nextpnr flow can be found
on [arXiv](https://arxiv.org/abs/1903.10407).

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
- Boost libraries (`libboost-dev libboost-filesystem-dev libboost-thread-dev libboost-program-options-dev libboost-python-dev libboost-iostreams-dev libboost-dev` or `libboost-all-dev` for Ubuntu)
- Eigen3 (`libeigen3-dev` for Ubuntu) is required to build the analytic placer
- Latest git Yosys is required to synthesise the demo design
- For building on Windows with MSVC, usage of vcpkg is advised for dependency installation.
  - For 32 bit builds: `vcpkg install boost-filesystem boost-program-options boost-thread boost-python qt5-base eigen3`
  - For 64 bit builds: `vcpkg install boost-filesystem:x64-windows boost-program-options:x64-windows boost-thread:x64-windows boost-python:x64-windows qt5-base:x64-windows eigen3:x64-windows`
  - For static builds, add `-static` to each of the package names.  For example, change `eigen3:x64-windows` to `eigen3:x64-windows-static`
  - A copy of Python that matches the version in vcpkg (currently Python 3.6.4).  You can download the [Embeddable Zip File](https://www.python.org/downloads/release/python-364/) and extract it.  You may need to extract `python36.zip` within the embeddable zip file to a new directory called "Lib".
- For building on macOS, brew utility is needed.
  - Install all needed packages `brew install cmake python boost boost-python3 qt5 eigen`
  - Do not forget to add qt5 in path as well `echo 'export PATH="/usr/local/opt/qt/bin:$PATH"' >> ~/.bash_profile`

    NOTE: this change is effective in next terminal session, so please re-open terminal window before next step

Getting started
---------------

### nextpnr-ice40

For iCE40 support, install [Project IceStorm](http://www.clifford.at/icestorm/) to `/usr/local` or another location, which should be passed as `-DICESTORM_INSTALL_PREFIX=/usr` to CMake. Then build and install `nextpnr-ice40` using the following commands:

```
cmake . -DARCH=ice40
make -j$(nproc)
sudo make install
```

On Windows, you may specify paths explicitly:

```
cmake . -DARCH=ice40 -DICEBOX_INSTALL_PREFIX=C:/ProgramData/icestorm -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake -DVCPKG_TARGET_TRIPLET=x64-windows -G "Visual Studio 15 2017 Win64" -DPYTHON_EXECUTABLE=C:/Python364/python.exe -DPYTHON_LIBRARY=C:/vcpkg/packages/python3_x64-windows/lib/python36.lib -DPYTHON_INCLUDE_DIR=C:/vcpkg/packages/python3_x64-windows/include/python3.6 .
cmake --build . --config Release
```

To build a static release, change the target triplet from `x64-windows` to `x64-windows-static` and add `-DBUILD_STATIC=ON`.

A simple example that runs on the iCEstick dev board can be found in `ice40/examples/blinky/blinky.*`.
Usage example:

```
cd ice40/examples/blinky
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

For ECP5 support, install [Project Trellis](https://github.com/SymbiFlow/prjtrellis) to `/usr/local` or another location, which should be passed as `-DTRELLIS_INSTALL_PREFIX=/usr/local` to CMake. Then build and install `nextpnr-ecp5` using the following commands:

```
cmake . -DARCH=ecp5 -DTRELLIS_INSTALL_PREFIX=/usr
make -j$(nproc)
sudo make install
```

 - Examples of the ECP5 flow for a range of boards can be found in the [Project Trellis Examples](https://github.com/SymbiFlow/prjtrellis/tree/master/examples).


### nextpnr-generic

The generic target allows running placement and routing for arbitrary custom architectures.

```
cmake . -DARCH=generic
make -j$(nproc)
sudo make install
```

An example of how to use the generic flow is in [generic/examples](generic/examples). See also the [Generic Architecture docs](docs/generic.md).

### Multiple architectures

To build nextpnr for multiple architectures at once, a semicolon-separated list can be used with `-DARCH`.

```
cmake . -DARCH="ice40;ecp5"
make -j$(nproc)
sudo make install
```

To build every available architecture, use `-DARCH=all`.

Pre-generating chip databases
-----------------------------

It is possible to pre-generate chip databases (`.bba` files). This can come in handy when building on time-constrained cloud instances, or in situations where Python is unable to use modules. To do this, build the architecture as a standalone project, which will produce the chip database alone. For example, for iCE40:

```
cd ice40
cmake .
make
```

This will create a `chipdb` directory with `.bba` files. Provide the path to this directory when building nextpnr by using `-D<arch>_CHIPDB=/path/to/chipdb`.

Cross-compilation
-----------------

Apart from chip databases, nextpnr requires the `bba` tool to be compiled for the build system. This tool can be compiled as a separate project:

```
cd bba
cmake .
make
```

This will create a `bba-export.cmake` file. Provide the path to this file when cross-building nextpnr by using `-DBBA_IMPORT=/path/to/bba-export.cmake`.

Additional notes for building nextpnr
-------------------------------------

The following runs a debug build of the iCE40 architecture without GUI, without Python support, without the HeAP analytic placer and only HX1K support:

```
cmake . -DARCH=ice40 -DCMAKE_BUILD_TYPE=Debug -DBUILD_PYTHON=OFF -DBUILD_GUI=OFF -DBUILD_HEAP=OFF -DICE40_HX1K_ONLY=1
make -j$(nproc)
```

To make static build release for iCE40 architecture use the following:

```
cmake . -DARCH=ice40 -DBUILD_PYTHON=OFF -DBUILD_GUI=OFF -DSTATIC_BUILD=ON
make -j$(nproc)
```

The HeAP placer's solver can optionally use OpenMP for a speedup on very large designs. Enable this by passing `-DUSE_OPENMP=yes` to cmake (compiler support may vary).

You can change the location where nextpnr will be installed (this will usually default to `/usr/local`) by using `-DCMAKE_INSTALL_PREFIX=/install/prefix`.

Notes for developers
--------------------

- All code is formatted using `clang-format` according to the style rules in `.clang-format` (LLVM based with
  increased indent widths and brace wraps after classes).
- To automatically format all source code, run `make clangformat`.
- See the wiki for additional documentation on the architecture API.

Recording a movie
-----------------

- To save a movie recording of place-and-route click recording icon in toolbar and select empty directory
  where recording files will be stored and select frames to skip.
- Manualy start all PnR operations you wish
- Click on recording icon again to stop recording
- Go to directory containing files and exeecute `ffmpeg -f image2 -r 1 -i movie_%05d.png -c:v libx264 nextpnr.mp4`

Testing
-------

- To build test binaries as well, use `-DBUILD_TESTS=ON` and after `make` run `make test` to run them, or you can run separate binaries.
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
