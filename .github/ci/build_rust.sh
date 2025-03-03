#!/bin/bash

function get_dependencies {
    :
}

function build_nextpnr {
    mkdir build
    pushd build
    cmake .. -DARCH=generic -DWERROR=on -DBUILD_RUST=on
    make nextpnr-generic -j`nproc`
    popd
}

function run_tests {
    export PATH=${GITHUB_WORKSPACE}/.yosys/bin:$PATH
    ( export NPNR=$(pwd)/build/nextpnr-generic && cd tests/generic/flow && ./run.sh )
}

function run_archcheck {
    pushd build
    # This isn't strictly necessary, since it's tested by build_generic.sh
    # but it can't hurt to run it anyway.
    ./nextpnr-generic --uarch example --test
    popd
}
