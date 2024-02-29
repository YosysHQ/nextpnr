#!/bin/bash

function get_dependencies {
    :
}

function build_nextpnr {
    mkdir build
    pushd build
    cmake .. -DARCH=himbaechel -DHIMBAECHEL_EXAMPLE_DEVICES=example
    make nextpnr-himbaechel chipdb-himbaechel-example -j`nproc`
    popd
}

function run_tests {
    :
}

function run_archcheck {
    pushd build
    ./nextpnr-himbaechel --device EXAMPLE --test
    popd
}
