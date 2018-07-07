nextpnr -- a portable FPGA place and route tool
===============================================

Supported Architectures
-----------------------

- iCE40

Prequisites
-----------
 
 - CMake 3.3 or later
 - Modern C++11 compiler (`clang-format` required for development)
 - Qt5 or later (`qt5-default` for Ubuntu 16.04)
 - Python 3.5 or later, including development libraries (`python3-dev` for Ubuntu)
 - Boost libraries (`libboost-dev` or `libboost-all-dev` for Ubuntu)
 - Icestorm, with chipdbs installed in `/usr/local/share/icebox`
 - Latest git Yosys is required to synthesise the demo design
 - For building on Windows with MSVC, usage of vcpkg is advised for dependency installation.
     - For 32 bit builds: `vcpkg install boost-filesystem boost-program-options boost-thread boost-python qt5-base`
     - For 64 bit builds: `vcpkg install boost-filesystem:x64-windows boost-program-options:x64-windows boost-thread:x64-windows boost-python:x64-windows qt5-base:x64-windows`
 - For building on macOS, brew utility is needed.
     - Install all needed packages `brew install cmake python boost boost-python3 qt5`
     - Do not forget to add qt5 in path as well `echo 'export PATH="/usr/local/opt/qt/bin:$PATH"' >> ~/.bash_profile`

Building
--------

 - Use CMake to generate the Makefiles (only needs to be done when `CMakeLists.txt` changes)
    - For a debug build, run `cmake -DCMAKE_BUILD_TYPE=Debug .`
    - For a debug build with HX1K support only, run ` cmake -DCMAKE_BUILD_TYPE=Debug -DICE40_HX1K_ONLY=1 .`
    - For a release build, run `cmake .`
    - Add `-DCMAKE_INSTALL_PREFIX=/your/install/prefix` to use a different install prefix to the default `/usr/local`
    - For MSVC build with vcpkg use `cmake . -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake` using your vcpkg location
    - For MSVC x64 build adding `-G"Visual Studio 14 2015 Win64"` is needed.
 - Use Make to run the build itself
    - For all binary targets, just run `make`
    - For just the iCE40 CLI&GUI binary, run `make nextpnr-ice40`
    - To build binary without Python support, run `cmake -DBUILD_PYTHON=OFF .`
    - To build binary without GUI, run `cmake -DBUILD_GUI=OFF .`
    - For minimal binary without Python and GUI, run `cmake -DBUILD_PYTHON=OFF -DBUILD_GUI=OFF .`
    - For just the iCE40 Python module, run `make nextpnrpy_ice40`
    - Using too many parallel jobs may lead to out-of-memory issues due to the significant memory needed to build the chipdbs
    - To install nextpnr, run `make install`

Testing
-------

 - To build test binaries as well, run `cmake -DBUILD_TESTS=OFF .` and after run `make tests` to run them, or you can run separate binaries.
 - To use code sanitizers use:
    - `cmake . -DSANITIZE_ADDRESS=ON`
    - `cmake . -DSANITIZE_MEMORY=ON -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++`
    - `cmake . -DSANITIZE_THREAD=ON`
    - `cmake . -DSANITIZE_UNDEFINED=ON`
 - Running valgrind example `valgrind --leak-check=yes --tool=memcheck ./nextpnr-ice40 --json ice40/blinky.json`

Running
--------

 - To run the CLI binary, just run `./nextpnr-ice40` (you should see command line help)
 - To start the UI, run `./nextpnr-ice40 --gui`
 - The Python module is called `nextpnrpy_ice40.so`. To test it, run `PYTHONPATH=. python3 python/python_mod_test.py`
 - Run `yosys blinky.ys` in `ice40/` to synthesise the blinky design and 
   produce `blinky.json`.
 - To place-and-route the blinky using nextpnr, run `./nextpnr-ice40 --hx1k --json ice40/blinky.json --pcf ice40/blinky.pcf --asc blinky.asc`

Notes
-------
 
 - All code is formatted using `clang-format` according to the style rules in `.clang-format` (LLVM based with 
 increased indent widths and brace wraps after classes).
 - To automatically format all source code, run `make clangformat`.
