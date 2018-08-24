#!/bin/bash

# This script is entirely self contained, so you can cache off of its' content
# the results stored in ICESTORM_ROOT and TRELLIS_ROOT.

set -e -x -o pipefail

# Parameters
ICESTORM_ROOT=${ICESTORM_ROOT:-/usr/local/icestorm}
TRELLIS_ROOT=${TRELLIS_ROOT:-/usr/local/trellis}

# Configuration
ICESTORM_REVISION=c02a4000f4cef8d4d9e76757f55ea4920667e1e8
TRELLIS_REVISION=f57e0f90b9f9d95a172adf56376fa875f36a7a4e
TRELLISDB_REVISION=ef5980c129be05aceeaea3b3c968d3102f8450b5

# Instal Icestorm
(
    cd /tmp
    rm -rf icestorm
    git clone https://github.com/cliffordwolf/icestorm.git
    cd icestorm
    git reset --hard $ICETORM_REVISION
    make install -j2 PREFIX=$ICESTORM_ROOT
    rm -rf icestorm
)

# Install Trellis
(
    mkdir -p $TRELLIS_ROOT
    cd $(dirname $TRELLIS_ROOT)
    rm -rf $(basename $TRELLIS_ROOT)
    git clone https://github.com/SymbiFlow/prjtrellis.git $(basename $TRELLIS_ROOT)
    cd $(basename $TRELLIS_ROOT)

    git reset --hard $TRELLIS_REVISION
    git submodule update --init --recursive
    . environment.sh
    rm -rf database
    git clone https://github.com/SymbiFlow/prjtrellis-db.git database
    cd database
    git reset --hard $TRELLISDB_REVISION
    cd ..
    cd libtrellis 
    cmake .
    make -j8
)
