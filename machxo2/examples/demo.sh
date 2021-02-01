#!/bin/sh

if [ ! -z ${TRELLIS_DB+x} ]; then
    DB_ARG="--db $TRELLIS_DB"
fi

${YOSYS:-yosys} -p 'synth_machxo2 -json tinyfpga.json' tinyfpga.v
${NEXTPNR:-../../nextpnr-machxo2} --1200 --package QFN32 --no-iobs --json tinyfpga.json --textcfg tinyfpga.txt
ecppack --compress $DB_ARG tinyfpga.txt tinyfpga.bit
tinyproga -b tinyfpga.bit
