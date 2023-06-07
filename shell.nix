{ pkgs ? import (fetchTarball https://github.com/NixOS/nixpkgs/archive/23.05.tar.gz) {} }:

let
  pythonPkgs = pkgs.python3Packages;
  boostPython = pkgs.boost.override { python = pythonPkgs.python; enablePython = true; };
  vscode = pkgs.vscode-with-extensions.override {
    vscodeExtensions = with pkgs.vscode-extensions; [
      bbenoist.nix
      ms-vscode.cpptools
      ms-vscode.cmake-tools
      twxs.cmake
      usernamehw.errorlens
      llvm-vs-code-extensions.vscode-clangd
    ] ++ pkgs.vscode-utils.extensionsFromVscodeMarketplace [
      {
        name = "VerilogHDL";
        publisher = "mshr-h";
        version = "1.11.4";
        sha256 = "sha256-4JY0eaN2IkwHv8u8X6ejDXk6vT1qB4vJjWdIy8b/jj4=";
      }
    ];
  };
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
    vscode
  ];

  shellHook = ''
    export TRELLIS_INSTALL_PREFIX=${pkgs.trellis}
    export ICESTORM_INSTALL_PREFIX=${pkgs.icestorm}
    export QT_QPA_PLATFORM_PLUGIN_PATH="${pkgs.libsForQt5.qt5.qtbase.bin}/lib/qt-${pkgs.libsForQt5.qt5.qtbase.version}/plugins";
  '';
}
