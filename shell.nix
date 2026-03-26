{ pkgs ? import (fetchTarball "https://github.com/NixOS/nixpkgs/archive/25.11.tar.gz") {} }:

let
  inherit (pkgs) stdenv lib;
  inherit (stdenv.hostPlatform) isDarwin;
  pythonPkgs = pkgs.python3Packages;
  boostPython = pkgs.boost.override { python = pythonPkgs.python; enablePython = true; };
in pkgs.mkShell {
  buildInputs = with pkgs; [
    cmake
    ninja
    eigen
    boostPython
    pythonPkgs.python
    pythonPkgs.apycula
    pkgs.qt6.qtbase
    lld
    llvmPackages.openmp
    icestorm
    trellis
    yosys
    clang
    gdb
    sccache
  ] ++ lib.optional (!isDarwin) valgrind;

  shellHook = ''
    export TRELLIS_INSTALL_PREFIX=${pkgs.trellis}
    export ICESTORM_INSTALL_PREFIX=${pkgs.icestorm}
    export QT_QPA_PLATFORM_PLUGIN_PATH="${pkgs.qt6.qtbase}/${pkgs.qt6.qtbase.qtPluginPrefix}"

    export CMAKE_C_COMPILER=clang
    export CMAKE_CXX_COMPILER=clang++
    export CMAKE_C_COMPILER_LAUNCHER=sccache
    export CMAKE_CXX_COMPILER_LAUNCHER=sccache
    export CMAKE_LINKER_TYPE=LLD
    export CMAKE_EXPORT_COMPILE_COMMANDS=ON
    export CMAKE_GENERATOR=Ninja
  '';
}
