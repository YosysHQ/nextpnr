#!/usr/bin/env bash
set -ex
echo "Running archcheck!"
${BUILD_DIR}/nextpnr-ice40 --hx8k --package ct256 --test
${BUILD_DIR}/nextpnr-ice40 --up5k --package sg48 --test
${BUILD_DIR}/nextpnr-ecp5 --um5g-25k --package CABGA381 --test
