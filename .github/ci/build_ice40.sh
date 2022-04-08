#!/bin/bash

function get_dependencies {
    :
}

function build_nextpnr {
    mkdir build
    pushd build
    cmake .. -DARCH=ice40 -DICESTORM_INSTALL_PREFIX=${GITHUB_WORKSPACE}/.icestorm -DWERROR=on -DBUILD_TESTS=on -DBUILD_GUI=on
    make nextpnr-ice40 nextpnr-ice40-test -j`nproc`
    popd
}

function run_tests {
    export PATH=${GITHUB_WORKSPACE}/.yosys/bin:${GITHUB_WORKSPACE}/.icestorm/bin:$PATH
    (cd build && ./nextpnr-ice40-test)
    (export NEXTPNR=$(pwd)/build/nextpnr-ice40 && cd ice40/smoketest/attosoc && ./smoketest.sh)
    make -j $(nproc) -C tests/ice40/regressions NPNR=$(pwd)/build/nextpnr-ice40
}

function run_archcheck {
    pushd build
    ./nextpnr-ice40 --hx8k --package ct256 --test
    ./nextpnr-ice40 --up5k --package sg48 --test
    popd
}
