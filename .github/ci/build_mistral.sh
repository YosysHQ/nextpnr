#!/bin/bash

export MISTRAL_PATH=${DEPS_PATH}/mistral

function get_dependencies {
    # Fetch mistral
    mkdir -p ${MISTRAL_PATH}
    git clone --recursive https://github.com/Ravenslofty/mistral.git ${MISTRAL_PATH}
    pushd ${MISTRAL_PATH}
    git reset --hard ${MISTRAL_REVISION}
    popd
}

function build_nextpnr {
    mkdir build
    pushd build
    cmake .. -DARCH=mistral -DMISTRAL_ROOT=${MISTRAL_PATH}
    make nextpnr-mistral -j`nproc`
    popd
}

function run_tests {
    :
}

function run_archcheck {
    pushd build
    ./nextpnr-mistral --device 5CEBA2F17A7 --test
    popd
}
