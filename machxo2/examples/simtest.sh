#!/usr/bin/env bash

if [ $# -lt 2 ]; then
    echo "Usage: $0 prefix mode"
    exit -1
fi

case $2 in
    "pack")
        NEXTPNR_MODE="--pack-only"
        ;;
    "place")
        NEXTPNR_MODE="--no-route"
        ;;
    "pnr")
        NEXTPNR_MODE=""
        ;;
    *)
        echo "Mode string must be \"pack\", \"place\", or \"pnr\""
        exit -2
        ;;
esac

if [ ! -f ${1}_tb.v ]; then
    echo "No testbench file (${1}_tb.v) found for ${1}.v"
    exit -3
fi

set -ex

${YOSYS:-yosys} -p "read_verilog ${1}.v
                    synth_lattice -family xo2 -json ${1}.json"
${NEXTPNR:-../../nextpnr-machxo2} $NEXTPNR_MODE --device LCMXO2-1200HC-4SG32C --json ${1}.json --write ${2}${1}.json
${YOSYS:-yosys} -p "read_verilog -lib +/lattice/cells_sim_xo2.v
                    read_json ${2}${1}.json
                    clean -purge
                    write_verilog -noattr -norename ${2}${1}.v"
iverilog -o ${1}_simtest ${CELLS_SIM:-`${YOSYS:yosys}-config --datdir/lattice/cells_sim_xo2.v`} ${1}_tb.v ${2}${1}.v
vvp -N ./${1}_simtest
