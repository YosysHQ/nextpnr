#!/bin/bash

function get_dependencies {
    pip3 install apycula==${APYCULA_REVISION}
}

function build_nextpnr {
    mkdir build
    pushd build
    cmake .. -DARCH=gowin -DWERROR=on -DBUILD_GUI=on -DUSE_IPO=off
    make nextpnr-gowin -j`nproc`
    popd
}

function run_tests {
    :
}

function run_archcheck {
    pushd build
    ./nextpnr-gowin --device GW1N-UV4LQ144C6/I5 --test
    popd
}
