#!/usr/bin/env bash
set -ex
yosys -p "synth_xilinx -flatten -abc9 -nobram -arch xc7 -top top; write_json blinky.json" blinky.v
nextpnr-himbaechel --device xc7a35tcsg324-1 -o xdc=arty.xdc --json blinky.json -o fasm=blinky.fasm --router router2
../bitgen_xray.sh xc7a35tcsg324-1 blinky.fasm blinky.bit
