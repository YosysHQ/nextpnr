#!/usr/bin/env bash
set -e

if [ -z "$PRJXRAY" ]; then
	echo "\$PRJXRAY must be set to a path to the prjxray repo"
	exit 2
fi
if [ -z "$PRJXRAY_DB" ]; then
	echo "\$PRJXRAY_DB must be set to a path to the prjxray-db repo"
	exit 2
fi

if [ -z "$1" ] || [ -z "$2" ] || [ -z "$3" ]; then
	echo "Usage: bitgen_xray.sh <device> <design.fasm> <design.bit>"
	exit 2
fi

DEVICE="$1"

FRAMES="$3.frames"

if [[ "$DEVICE" =~ xc7a.* ]]; then
	FAMILY=artix7
elif [[ "$DEVICE" =~ xc7z.* ]]; then
	FAMILY=zynq7
elif [[ "$DEVICE" =~ xc7k.* ]]; then
	FAMILY=kintex7
else
	echo "Unknown device $DEVICE"
	exit 2
fi

python3 ${PRJXRAY}/utils/fasm2frames.py --part "${DEVICE}" --db-root "${PRJXRAY_DB}/${FAMILY}" "$2" "$FRAMES"
${PRJXRAY}/build/tools/xc7frames2bit --part_file "${PRJXRAY_DB}/${FAMILY}/${DEVICE}/part.yaml" --part_name ${DEVICE} --frm_file "$FRAMES" --output_file "$3"
