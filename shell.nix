{ pkgs ? import (fetchTarball https://github.com/NixOS/nixpkgs/archive/24.05.tar.gz) {} }:

let
  pythonPkgs = pkgs.python3Packages;
  boostPython = pkgs.boost.override { python = pythonPkgs.python; enablePython = true; };
in pkgs.mkShell {
  buildInputs = with pkgs; [
    cmake
    eigen
    boostPython
    pythonPkgs.python
    pythonPkgs.apycula
    libsForQt5.qt5.qtbase
    llvmPackages.openmp
    icestorm
    trellis
    mold
    yosys
    clang
    valgrind
    cling
    gdb
  ];

  shellHook = ''
    export TRELLIS_INSTALL_PREFIX=${pkgs.trellis}
    export ICESTORM_INSTALL_PREFIX=${pkgs.icestorm}
    export QT_QPA_PLATFORM_PLUGIN_PATH="${pkgs.libsForQt5.qt5.qtbase.bin}/lib/qt-${pkgs.libsForQt5.qt5.qtbase.version}/plugins";
  '';
}
