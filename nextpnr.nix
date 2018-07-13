#
# nextpnr -- Next Generation Place and Route
#
# Copyright (C) 2018  Serge Bazanski  <q3k@symbioticeda.com>
#
# Permission to use, copy, modify, and/or distribute this software for any
# purpose with or without fee is hereby granted, provided that the above
# copyright notice and this permission notice appear in all copies.
#
# THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
# WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
# MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
# ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
# WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
# ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
# OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
#

with import <nixpkgs> {};

let
  gitRev    = "a8c84e90a39c54174dd24b5b76bd17aed8311481";
  gitBranch = "master";
in
  stdenv.mkDerivation rec {
    name    = "nextpnr-${version}";
    version = "1.0.0";

    src = ./.;

    buildInputs = [
      python3 (boost.override { python = python3; })
      qt5.qtbase
    ];

    nativeBuildInputs = [ cmake icestorm ];

    cmakeFlags= [
      "-DARCH=ice40"
      "-DICEBOX_ROOT=${icestorm}/share/icebox"
    ];

    enableParallelBuilding = true;

    meta = with stdenv.lib; {
      description = "Next Generation Place-and-Route tool for FPGAs";
      homepage    = "https://gitlab.com/symbioticeda/nextpnr";
      license     = licenses.bsd0;
      platforms   = platforms.linux;
    };
  }


