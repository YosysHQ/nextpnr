#!/bin/sh

if [ $# -lt 1 ]; then
    echo "Usage: $0 prefix"
    exit -1
fi

if ! grep -q "LOC" $1.vhd; then
    echo "$1.vhd does not have LOC constraints for tinyfpga_a."
    exit -2
fi

if [ ! -z ${TRELLIS_DB+x} ]; then
    DB_ARG="--db $TRELLIS_DB"
fi

set -ex

${YOSYS:-yosys} -p "ghdl --std=08 prims.vhd ${1}.vhd -e;
                    attrmap -tocase LOC
                    synth_lattice -family xo2 -json ${1}-vhdl.json"
${NEXTPNR:-../../nextpnr-machxo2} --device LCMXO2-1200HC-4SG32C --json $1-vhdl.json --textcfg $1-vhdl.txt
ecppack --compress $DB_ARG $1-vhdl.txt $1-vhdl.bit
tinyproga -b $1-vhdl.bit
