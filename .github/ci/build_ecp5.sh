#!/bin/bash

function get_dependencies {
    :
}

function build_nextpnr {
    mkdir build
    pushd build
    cmake .. -DARCH=ecp5 -DTRELLIS_INSTALL_PREFIX=${GITHUB_WORKSPACE}/.trellis -DWERROR=on -DBUILD_GUI=on -DUSE_IPO=off
    make nextpnr-ecp5 -j`nproc`
    popd
}

function run_tests {
    export PATH=${GITHUB_WORKSPACE}/.trellis/bin:${GITHUB_WORKSPACE}/.yosys/bin:$PATH
    make -j $(nproc) -C tests/ecp5/regressions NPNR=$(pwd)/build/nextpnr-ecp5
}

function run_archcheck {
    pushd build
    ./nextpnr-ecp5 --um5g-25k --package CABGA381 --test
    popd
}
