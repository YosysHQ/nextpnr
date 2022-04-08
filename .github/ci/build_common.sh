#!/bin/bash

# Install latest Yosys
function build_yosys {
    PREFIX=`pwd`/.yosys
    YOSYS_PATH=${DEPS_PATH}/yosys
    mkdir -p ${YOSYS_PATH}
    git clone --recursive https://github.com/YosysHQ/yosys ${YOSYS_PATH}
    pushd ${YOSYS_PATH}
    git reset --hard ${YOSYS_REVISION}
    make -j`nproc` PREFIX=$PREFIX
    make install PREFIX=$PREFIX
    popd
}

function build_icestorm {
    PREFIX=`pwd`/.icestorm
    ICESTORM_PATH=${DEPS_PATH}/icestorm
    mkdir -p ${ICESTORM_PATH}
    git clone --recursive https://github.com/YosysHQ/icestorm ${ICESTORM_PATH}
    pushd ${ICESTORM_PATH}
    git reset --hard ${ICESTORM_REVISION}
    make -j`nproc` PREFIX=${PREFIX}
    make install PREFIX=${PREFIX}
    popd
}

function build_trellis {
    PREFIX=`pwd`/.trellis
    TRELLIS_PATH=${DEPS_PATH}/prjtrellis
    mkdir -p ${TRELLIS_PATH}
    git clone --recursive https://github.com/YosysHQ/prjtrellis ${TRELLIS_PATH}
    pushd ${TRELLIS_PATH}
    git reset --hard ${TRELLIS_REVISION}
    mkdir -p libtrellis/build
    pushd libtrellis/build
    cmake -DCMAKE_INSTALL_PREFIX=${PREFIX} ..
    make -j`nproc`
    make install
    popd
    popd
}

function build_prjoxide {
    PREFIX=`pwd`/.prjoxide
    PRJOXIDE_PATH=${DEPS_PATH}/prjoxide
    curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh -s -- -y ;\
    mkdir -p ${PRJOXIDE_PATH}
    git clone --recursive https://github.com/gatecat/prjoxide ${PRJOXIDE_PATH}
    pushd ${PRJOXIDE_PATH}
    git reset --hard ${PRJOXIDE_REVISION}
    cd libprjoxide
    PATH=$PATH:$HOME/.cargo/bin cargo install --root $PREFIX --path prjoxide
    popd
}
