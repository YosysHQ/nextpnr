#!/usr/bin/env bash

if [ $# -lt 1 ]; then
    echo "Usage: $0 nextpnr_mode solve_mode"
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
        echo "nextpnr_mode string must be \"pack\", \"place\", or \"pnr\""
        exit -2
        ;;
esac

case $2 in
    "sat")
        SAT=1
        ;;
    "smt")
        SMT=1
        ;;
    *)
        echo "solve_mode string must be \"sat\", or \"smt\""
        exit -3
        ;;
esac

do_sat() {
    ${YOSYS:-yosys} -l ${1}miter_sat.log -p "read_verilog blinky.v
                        rename top gold
                        read_verilog ${1}blinky.v
                        rename top gate
                        read_verilog +/machxo2/cells_sim.v

                        miter -equiv -make_assert -flatten gold gate ${1}miter
                        hierarchy -top ${1}miter
                        sat -verify -prove-asserts -tempinduct ${1}miter"
}

do_smt() {
    ${YOSYS:-yosys} -l ${1}miter_smt.log -p "read_verilog blinky.v
                        rename top gold
                        read_verilog ${1}blinky.v
                        rename top gate
                        read_verilog +/machxo2/cells_sim.v

                        miter -equiv -make_assert gold gate ${1}miter
                        hierarchy -auto-top -check; proc;
                        opt_clean
                        write_verilog ${1}miter.v
                        write_smt2 ${1}miter.smt2"

    yosys-smtbmc -s z3 --dump-vcd ${1}miter_bmc.vcd ${1}miter.smt2
    yosys-smtbmc -s z3 -i --dump-vcd ${1}miter_tmp.vcd ${1}miter.smt2
}

set -ex

${YOSYS:-yosys} -p "read_verilog blinky.v
                    synth_machxo2 -noiopad -json blinky.json
                    show -format png -prefix blinky"
${NEXTPNR:-../../nextpnr-machxo2} $NEXTPNR_MODE --1200 --no-iobs --json blinky.json --write ${1}blinky.json
${YOSYS:-yosys} -p "read_verilog -lib +/machxo2/cells_sim.v
                    read_json ${1}blinky.json
                    clean -purge
                    show -format png -prefix ${1}blinky
                    write_verilog -noattr -norename ${1}blinky.v"

if [ $2 = "sat" ]; then
    do_sat $1
elif [ $2 = "smt" ]; then
    do_smt $1
fi
