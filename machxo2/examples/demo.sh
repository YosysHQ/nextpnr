#!/bin/sh

if [ $# -lt 1 ]; then
    echo "Usage: $0 prefix"
    exit -1
fi

if ! grep -q "(\*.*LOC.*\*)" $1.v; then
    echo "$1.v does not have LOC constraints for tinyfpga_a."
    exit -2
fi

if [ ! -z ${TRELLIS_DB+x} ]; then
    DB_ARG="--db $TRELLIS_DB"
fi

set -ex

${YOSYS:-yosys} -p "synth_machxo2 -json $1.json" $1.v
${NEXTPNR:-../../nextpnr-machxo2} --1200 --package QFN32 --json $1.json --textcfg $1.txt
ecppack --compress $DB_ARG $1.txt $1.bit
tinyproga -b $1.bit
