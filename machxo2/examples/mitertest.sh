#!/usr/bin/env bash

if [ $# -lt 3 ]; then
    echo "Usage: $0 prefix nextpnr_mode solve_mode"
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
        echo "nextpnr_mode string must be \"pack\", \"place\", or \"pnr\""
        exit -2
        ;;
esac

case $3 in
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
    ${YOSYS:-yosys} -l ${2}${1}_miter_sat.log -p "read_verilog ${1}.v
                        rename top gold
                        read_verilog ${2}${1}.v
                        rename top gate
                        read_verilog +/machxo2/cells_sim.v

                        miter -equiv -make_assert -flatten gold gate ${2}${1}_miter
                        hierarchy -top ${2}${1}_miter
                        sat -verify -prove-asserts -tempinduct ${2}${1}_miter"
}

do_smt() {
    ${YOSYS:-yosys} -l ${2}${1}_miter_smt.log -p "read_verilog ${1}.v
                        rename top gold
                        read_verilog ${2}${1}.v
                        rename top gate
                        read_verilog +/machxo2/cells_sim.v

                        miter -equiv -make_assert gold gate ${2}${1}_miter
                        hierarchy -auto-top -check; proc;
                        opt_clean
                        write_verilog ${2}${1}_miter.v
                        write_smt2 ${2}${1}_miter.smt2"

    yosys-smtbmc -s z3 --dump-vcd ${2}${1}_miter_bmc.vcd ${2}${1}_miter.smt2
    yosys-smtbmc -s z3 -i --dump-vcd ${2}${1}_miter_tmp.vcd ${2}${1}_miter.smt2
}

set -ex

${YOSYS:-yosys} -p "read_verilog ${1}.v
                    synth_machxo2 -noiopad -json ${1}.json"
# FIXME: --json option really not needed here.
${NEXTPNR:-../../nextpnr-machxo2} $NEXTPNR_MODE --1200 --package QFN32 --no-iobs --json ${1}.json --write ${2}${1}.json
${YOSYS:-yosys} -p "read_verilog -lib +/machxo2/cells_sim.v
                    read_json ${2}${1}.json
                    clean -purge
                    show -format png -prefix ${2}${1}
                    write_verilog -noattr -norename ${2}${1}.v"

if [ $3 = "sat" ]; then
    do_sat $1 $2
elif [ $3 = "smt" ]; then
    do_smt $1 $2
fi
