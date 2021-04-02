#!/bin/bash

# Install capnproto libraries
function build_capnp {
    curl -O https://capnproto.org/capnproto-c++-0.8.0.tar.gz
    tar zxf capnproto-c++-0.8.0.tar.gz
    pushd capnproto-c++-0.8.0
    ./configure
    make -j`nproc` check
    sudo make install
    popd

    git clone https://github.com/capnproto/capnproto-java.git
    pushd capnproto-java
    make -j`nproc`
    sudo make install
    popd
}

# Install latest Yosys
function build_yosys {
    DESTDIR=`pwd`/.yosys
    pushd yosys
    make -j`nproc`
    sudo make install DESTDIR=$DESTDIR
    popd
}


function get_dependencies {
    # Install python-fpga-interchange libraries
    git clone -b ${PYTHON_INTERCHANGE_TAG} https://github.com/SymbiFlow/python-fpga-interchange.git ${PYTHON_INTERCHANGE_PATH}
    pushd ${PYTHON_INTERCHANGE_PATH}
    git submodule update --init --recursive
    python3 -m pip install -r requirements.txt
    popd

    if [ ${DEVICE} == "LIFCL-17" ]; then
        # Install prjoxide
        curl --proto '=https' -sSf https://sh.rustup.rs | sh -s -- -y
        git clone --recursive https://github.com/gatecat/prjoxide.git
        pushd prjoxide/libprjoxide
        PATH=$PATH:$HOME/.cargo/bin cargo install --path prjoxide --all-features
        popd
    else
        # Install RapidWright
        git clone https://github.com/Xilinx/RapidWright.git ${RAPIDWRIGHT_PATH}
        pushd ${RAPIDWRIGHT_PATH}
        make update_jars
        popd
    fi
}

function build_nextpnr {
    build_capnp
    mkdir build
    pushd build
    cmake .. -DARCH=fpga_interchange -DRAPIDWRIGHT_PATH=${RAPIDWRIGHT_PATH} -DPYTHON_INTERCHANGE_PATH=${PYTHON_INTERCHANGE_PATH}
    make nextpnr-fpga_interchange -j`nproc`
    popd
}
