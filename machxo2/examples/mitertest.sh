#!/usr/bin/env bash

if [ $# -lt 3 ]; then
    echo "Usage: $0 prefix nextpnr_mode solve_mode"
    exit -1
fi

if grep -q "OSCH" $1.v; then
    echo "$1.v uses blackbox primitive OSCH and cannot be simulated."
    exit -2
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
        exit -3
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
        exit -4
        ;;
esac

do_sat() {
    ${YOSYS:-yosys} -l ${2}${1}_miter_sat.log -p "read_verilog ${1}.v
                        rename top gold
                        read_verilog ${2}${1}.v
                        rename top gate
                        read_verilog +/lattice/cells_sim_xo2.v

                        miter -equiv -make_assert -flatten gold gate ${2}${1}_miter
                        hierarchy -top ${2}${1}_miter
                        sat -verify -prove-asserts -tempinduct ${2}${1}_miter"
}

do_smt() {
    ${YOSYS:-yosys} -l ${2}${1}_miter_smt.log -p "read_verilog ${1}.v
                        rename top gold
                        read_verilog ${2}${1}.v
                        rename top gate
                        read_verilog +/lattice/cells_sim_xo2.v

                        miter -equiv -make_assert gold gate ${2}${1}_miter
                        hierarchy -top ${2}${1}_miter; proc;
                        opt_clean
                        flatten t:*FACADE_IO*
                        write_verilog ${2}${1}_miter.v
                        write_smt2 ${2}${1}_miter.smt2"

    yosys-smtbmc -s z3 --dump-vcd ${2}${1}_miter_bmc.vcd ${2}${1}_miter.smt2
    yosys-smtbmc -s z3 -i --dump-vcd ${2}${1}_miter_tmp.vcd ${2}${1}_miter.smt2
}

set -ex

${YOSYS:-yosys} -p "read_verilog ${1}.v
                    synth_lattice -family xo2 -json ${1}.json"
${NEXTPNR:-../../nextpnr-machxo2} $NEXTPNR_MODE --device LCMXO2-1200HC-4SG32C --json ${1}.json --write ${2}${1}.json
${YOSYS:-yosys} -p "read_verilog -lib +/lattice/cells_sim_xo2.v
                    read_json ${2}${1}.json
                    clean -purge
                    write_verilog -noattr -norename ${2}${1}.v"

if [ $3 = "sat" ]; then
    do_sat $1 $2
elif [ $3 = "smt" ]; then
    do_smt $1 $2
fi
