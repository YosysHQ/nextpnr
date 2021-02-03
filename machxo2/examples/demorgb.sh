#!/bin/sh

if [ ! -z ${TRELLIS_DB+x} ]; then
    DB_ARG="--db $TRELLIS_DB"
fi

${YOSYS:-yosys} -p 'synth_machxo2 -json rgbcount.json' rgbcount.v
${NEXTPNR:-../../nextpnr-machxo2} --1200 --package QFN32 --no-iobs --json rgbcount.json --textcfg rgbcount.txt
ecppack --compress $DB_ARG rgbcount.txt rgbcount.bit
tinyproga -b rgbcount.bit
