{
  description = "A flake for the nextpnr FPGA place and route tool";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, flake-utils, ... }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = import nixpkgs {
          inherit system;
          overlays = [];
        };
        # Define custom overrides or additional packages
        myPythonPackages = pkgs.python3Packages.override {
          overrides = self: super: {
            apycula = super.apycula.override {
              # Place any necessary overrides here
            };
          };
        };
        enableGui = false;
      in
        {
          packages = {
            nextpnr = pkgs.stdenv.mkDerivation {
              pname = "nextpnr";
              version = "0.6";
              src = ./.;

              nativeBuildInputs = [ pkgs.cmake ]
                                  ++ (pkgs.lib.optional enableGui pkgs.qt6.wrapQtAppsHook);
              buildInputs = [
                (pkgs.boost.override { python = pkgs.python3; enablePython = true; })
                pkgs.python3
                pkgs.eigen
                myPythonPackages.apycula
                pkgs.icestorm
                pkgs.trellis
                pkgs.tcl
                pkgs.zlib
                pkgs.capnproto
                pkgs.lzma
                pkgs.tk
                pkgs.wget
              ] ++ (pkgs.lib.optional enableGui pkgs.qt6.qtbase)
              ++ (pkgs.lib.optional pkgs.stdenv.cc.isClang pkgs.llvmPackages.openmp);

              cmakeFlags = [
                "-DARCH=all"
                "-DBUILD_TESTS=ON"
                "-DICESTORM_INSTALL_PREFIX=${pkgs.icestorm}"
                "-DTRELLIS_INSTALL_PREFIX=${pkgs.trellis}"
                "-DTRELLIS_LIBDIR=${pkgs.trellis}/lib/trellis"
                "-DGOWIN_BBA_EXECUTABLE=${myPythonPackages.apycula}/bin/gowin_bba"
                "-DUSE_OPENMP=ON"
                # warning: high RAM usage
                "-DSERIALIZE_CHIPDBS=OFF"
              ] ++ (pkgs.lib.optional enableGui "-DBUILD_GUI=ON");

              meta = with pkgs.lib; {
                description = "Place and route tool for FPGAs";
                homepage = "https://github.com/yosyshq/nextpnr";
                license = licenses.isc;
                platforms = platforms.all;
                maintainers = with maintainers; [ ];
              };
            };
          };

          packages.default = self.packages.${system}.nextpnr;

          devShell = pkgs.mkShell {
            inputsFrom = [ self.packages.${system}.default ];

            shellHook = ''
             export TRELLIS_INSTALL_PREFIX=${pkgs.trellis}
             export ICESTORM_INSTALL_PREFIX=${pkgs.icestorm}
             export QT_QPA_PLATFORM_PLUGIN_PATH="${pkgs.libsForQt5.qt5.qtbase.bin}/lib/qt-${pkgs.libsForQt5.qt5.qtbase.version}/plugins";
            '';
          };
        });
}
