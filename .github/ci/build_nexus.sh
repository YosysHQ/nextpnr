#!/bin/bash

function get_dependencies {
    :
}

function build_nextpnr {
    mkdir build
    pushd build
    cmake .. -DARCH=nexus -DOXIDE_INSTALL_PREFIX=${GITHUB_WORKSPACE}/.prjoxide
    make nextpnr-nexus -j`nproc`
    popd
}

function run_tests {
    :
}

function run_archcheck {
    pushd build
    ./nextpnr-nexus --device LIFCL-40-9BG400CES --test
    popd
}
