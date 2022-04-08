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
    :
}
