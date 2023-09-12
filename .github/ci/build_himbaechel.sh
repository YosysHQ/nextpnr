#!/bin/bash

function get_dependencies {
    :
}

function build_nextpnr {
    mkdir build
    pushd build
    cmake .. -DARCH=himbaechel
    make nextpnr-himbaechel bbasm -j`nproc`
    # We'd ideally use pypy3 for speed (as works locally), but the version
    # our CI Ubuntu provides doesn't like some of the typing stuff
    python3 ../himbaechel/uarch/example/example_arch_gen.py ./example.bba
    mkdir -p share/himbaechel/example
    ./bba/bbasm --l ./example.bba share/himbaechel/example/example.bin
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
