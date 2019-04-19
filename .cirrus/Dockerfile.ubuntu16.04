FROM ubuntu:xenial-20181113

ENV DEBIAN_FRONTEND=noninteractive

RUN set -e -x ;\
    apt-get -y update ;\
    apt-get -y upgrade ;\
    apt-get -y install \
        build-essential autoconf cmake clang bison wget flex gperf \
        libreadline-dev gawk tcl-dev libffi-dev graphviz xdot python3-dev \
        libboost-all-dev qt5-default git libftdi-dev pkg-config libeigen3-dev

RUN set -e -x ;\
    mkdir -p /usr/local/src ;\
    cd /usr/local/src ;\
    git clone --recursive https://github.com/steveicarus/iverilog.git ;\
    cd iverilog ;\
    git reset --hard 172d7eb0a3665f89b91d601b5912c33acedc81e5 ;\
    sh autoconf.sh ;\
    ./configure ;\
    make -j $(nproc) ;\
    make install ;\
    rm -rf /usr/local/src/iverilog

RUN set -e -x ;\
    mkdir -p /usr/local/src ;\
    cd /usr/local/src ;\
    git clone --recursive https://github.com/cliffordwolf/icestorm.git ;\
    cd icestorm ;\
    git reset --hard 3a2bfee5cbc0558641668114260d3f644d6b7c83 ;\
    make -j $(nproc) ;\
    make install

RUN set -e -x ;\
    mkdir -p /usr/local/src ;\
    cd /usr/local/src ;\
    git clone --recursive https://github.com/YosysHQ/yosys.git ;\
    cd yosys ;\
    git reset --hard ea8ac0aaad3a1f89ead8eb44b2fef5927f29a099 ;\
    make -j $(nproc) ;\
    make install ;\
    rm -rf /usr/local/src/yosys

RUN set -e -x ;\
    mkdir -p /usr/local/src ;\
    cd /usr/local/src ;\
    git clone --recursive https://github.com/SymbiFlow/prjtrellis.git ;\
    cd prjtrellis ;\
    git reset --hard 46e95314be7f8850db80ad1ef6b3359b091fad93 ;\
    cd libtrellis ;\
    cmake -DCMAKE_INSTALL_PREFIX=/usr . ;\
    make -j $(nproc) ;\
    make install
