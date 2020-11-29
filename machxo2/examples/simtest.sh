#!/usr/bin/env bash

if [ $# -lt 1 ]; then
    echo "Usage: $0 mode"
    exit -1
fi

case $1 in
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

set -ex

${YOSYS:-yosys} -p "read_verilog blinky.v
                    synth_machxo2 -json blinky.json
                    show -format png -prefix blinky"
${NEXTPNR:-../../nextpnr-machxo2} $NEXTPNR_MODE --1200 --no-iobs --json blinky.json --write ${1}blinky.json
${YOSYS:-yosys} -p "read_verilog -lib +/machxo2/cells_sim.v
                    read_json ${1}blinky.json
                    clean -purge
                    show -format png -prefix ${1}blinky
                    write_verilog -noattr -norename ${1}blinky.v"
iverilog -o blinky_simtest ${CELLS_SIM:-`${YOSYS:yosys}-config --datdir/machxo2/cells_sim.v`} blinky_tb.v ${1}blinky.v
vvp -N ./blinky_simtest
