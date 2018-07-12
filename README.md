nextpnr -- a portable FPGA place and route tool
===============================================

Supported Architectures
-----------------------

- iCE40
- ECP5

Prequisites
-----------
 
 - CMake 3.3 or later
 - Modern C++11 compiler (`clang-format` required for development)
 - Qt5 or later (`qt5-default` for Ubuntu 16.04)
 - Python 3.5 or later, including development libraries (`python3-dev` for Ubuntu)
    - on Windows make sure to install same version as supported by [vcpkg](https://github.com/Microsoft/vcpkg/blob/master/ports/python3/CONTROL)
 - Boost libraries (`libboost-dev` or `libboost-all-dev` for Ubuntu)
 - Icestorm, with chipdbs installed in `/usr/local/share/icebox`
 - Latest git Yosys is required to synthesise the demo design
 - For building on Windows with MSVC, usage of vcpkg is advised for dependency installation.
     - For 32 bit builds: `vcpkg install boost-filesystem boost-program-options boost-thread boost-python qt5-base`
     - For 64 bit builds: `vcpkg install boost-filesystem:x64-windows boost-program-options:x64-windows boost-thread:x64-windows boost-python:x64-windows qt5-base:x64-windows`
 - For building on macOS, brew utility is needed.
     - Install all needed packages `brew install cmake python boost boost-python3 qt5`
     - Do not forget to add qt5 in path as well `echo 'export PATH="/usr/local/opt/qt/bin:$PATH"' >> ~/.bash_profile`
 - For ECP5 support, you must download [Project Trellis](https://github.com/SymbiFlow/prjtrellis), then follow its instructions to
   download the latest database and build _libtrellis_.
 
 
Building
--------

 - Specifying target architecture is mandatory use ARCH parameter to set it. It is semicolon separated list.
    - Use `cmake . -DARCH=all` to build all supported targets
    - For example `cmake . -DARCH=ice40` would build just ICE40 support
 - Use CMake to generate the Makefiles (only needs to be done when `CMakeLists.txt` changes)
    - For an iCE40 debug build, run `cmake -DARCH=ice40 -DCMAKE_BUILD_TYPE=Debug .`
    - For an iCE40 debug build with HX1K support only, run `cmake -DARCH=ice40 -DCMAKE_BUILD_TYPE=Debug -DICE40_HX1K_ONLY=1 .`
    - For an iCE40 and ECP5 release build, run `cmake -DARCH="ice40;ecp5" .`
    - Add `-DCMAKE_INSTALL_PREFIX=/your/install/prefix` to use a different install prefix to the default `/usr/local`
    - For MSVC build with vcpkg use `-DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake` using your vcpkg location
    - For MSVC x64 build adding `-G"Visual Studio 14 2015 Win64"` is needed.
    - For ECP5 support, you must also specify the path to Project Trellis using `-DTRELLIS_ROOT=/path/trellis`
 - Use Make to run the build itself
    - For all binary targets, just run `make`
    - For just the iCE40 CLI&GUI binary, run `make nextpnr-ice40`
    - To build binary without Python support, use `-DBUILD_PYTHON=OFF`
    - To build binary without GUI, use `-DBUILD_GUI=OFF`
    - For minimal binary without Python and GUI, use `-DBUILD_PYTHON=OFF -DBUILD_GUI=OFF`
    - For just the iCE40 Python module, run `make nextpnrpy_ice40`
    - Using too many parallel jobs may lead to out-of-memory issues due to the significant memory needed to build the chipdbs
    - To install nextpnr, run `make install`

Testing
-------

 - To build test binaries as well, use `-DBUILD_TESTS=OFF` and after run `make tests` to run them, or you can run separate binaries.
 - To use code sanitizers use the `cmake` options:
    - `-DSANITIZE_ADDRESS=ON`
    - `-DSANITIZE_MEMORY=ON -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++`
    - `-DSANITIZE_THREAD=ON`
    - `-DSANITIZE_UNDEFINED=ON`
 - Running valgrind example `valgrind --leak-check=yes --tool=memcheck ./nextpnr-ice40 --json ice40/blinky.json`

Running
--------

 - To run the CLI binary, just run `./nextpnr-ice40` (you should see command line help)
 - To start the UI, run `./nextpnr-ice40 --gui`
 - The Python module is called `nextpnrpy_ice40.so`. To test it, run `PYTHONPATH=. python3 python/python_mod_test.py`
 - Run `yosys blinky.ys` in `ice40/` to synthesise the blinky design and 
   produce `blinky.json`.
 - To place-and-route the blinky using nextpnr, run `./nextpnr-ice40 --hx1k --json ice40/blinky.json --pcf ice40/blinky.pcf --asc blinky.asc`

 - For an ECP5 blinky, first synthesise using `yosys blinky.ys` in `ecp5/synth`.
 - Then run ECP5 place-and route using
 `./nextpnr-ecp5 --json ecp5/synth/blinky.json --basecfg ecp5/synth/ulx3s_empty.config --bit ecp5/synth/ulx3s.bit`
    - Note that `ulx3s_empty.config` contains fixed/unknown bits to be copied to the output bitstream
    - You can also use `--textcfg out.config` to write a text file describing the bitstream for debugging
  
Notes
-------
 
 - All code is formatted using `clang-format` according to the style rules in `.clang-format` (LLVM based with 
 increased indent widths and brace wraps after classes).
 - To automatically format all source code, run `make clangformat`.
