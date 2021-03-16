#!/bin/bash

# Install capnproto libraries
curl -O https://capnproto.org/capnproto-c++-0.7.0.tar.gz
tar zxf capnproto-c++-0.7.0.tar.gz
pushd capnproto-c++-0.7.0
./configure
make -j`nproc` check
sudo make install
popd

# Install latest Yosys
git clone https://github.com/YosysHQ/yosys.git
pushd yosys
make -j`nproc`
sudo make install
popd

# Install capnproto java
git clone https://github.com/capnproto/capnproto-java.git
pushd capnproto-java
make
sudo make install
popd

RAPIDWRIGHT_PATH="`pwd`/RapidWright"
INTERCHANGE_SCHEMA_PATH="`pwd`/3rdparty/fpga-interchange-schema/interchange"
PYTHON_INTERCHANGE_PATH="`pwd`/python-fpga-interchange"
PYTHON_INTERCHANGE_TAG="v0.0.1"

# Install python-fpga-interchange libraries
git clone -b $PYTHON_INTERCHANGE_TAG https://github.com/SymbiFlow/python-fpga-interchange.git $PYTHON_INTERCHANGE_PATH
pushd $PYTHON_INTERCHANGE_PATH
git submodule update --init --recursive
python3 -m pip install -r requirements.txt
popd

# Install RapidWright
git clone https://github.com/Xilinx/RapidWright.git $RAPIDWRIGHT_PATH
pushd $RAPIDWRIGHT_PATH
make update_jars
popd


mkdir build
pushd build
cmake .. -DARCH=fpga_interchange -DRAPIDWRIGHT_PATH=$RAPIDWRIGHT_PATH -DINTERCHANGE_SCHEMA_PATH=$INTERCHANGE_SCHEMA_PATH -DPYTHON_INTERCHANGE_PATH=$PYTHON_INTERCHANGE_PATH
popd
