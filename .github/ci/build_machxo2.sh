#!/bin/bash

function get_dependencies {
    :
}

function build_nextpnr {
    mkdir build
    pushd build
    cmake .. -DARCH=machxo2 -DTRELLIS_INSTALL_PREFIX=${GITHUB_WORKSPACE}/.trellis -DWERROR=on -DUSE_IPO=off
    make nextpnr-machxo2 -j`nproc`
    popd
}

function run_tests {
    :
}

function run_archcheck {
    pushd build
    ./nextpnr-machxo2 --device LCMXO2-1200HC-4SG32C --test
    ./nextpnr-machxo2 --device LCMXO3LF-6900C-6BG256C --test
    popd
}
