#!/bin/bash


export XRAY_DB_PATH=${DEPS_PATH}/prjxray-db
export PEPPERCORN_PATH=${DEPS_PATH}/prjpeppercorn


function get_dependencies {
    # Fetch prjxray-db
    git clone https://github.com/openXC7/prjxray-db ${XRAY_DB_PATH}
    # Fetch apycula
    pip install --break-system-packages apycula
    # Fetch prjpeppercorn
    git clone https://github.com/YosysHQ/prjpeppercorn ${PEPPERCORN_PATH}
}

function build_nextpnr {
    mkdir build
    pushd build
    cmake .. -DARCH=himbaechel -DHIMBAECHEL_UARCH="gowin;xilinx;example;gatemate" -DHIMBAECHEL_EXAMPLE_DEVICES=example \
        -D HIMBAECHEL_XILINX_DEVICES="xc7a50t" -D HIMBAECHEL_PRJXRAY_DB=${XRAY_DB_PATH} \
        -D HIMBAECHEL_GOWIN_DEVICES="GW1N-9C;GW5A-25A" \
        -D HIMBAECHEL_PEPPERCORN_PATH=${PEPPERCORN_PATH}
    make nextpnr-himbaechel -j`nproc`
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
