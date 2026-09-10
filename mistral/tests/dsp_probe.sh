#!/bin/sh
# Run on the designated target while holding its kit.py lease.
# Reads/writes HPS GP registers only; does not load or stop the FPGA.
set -eu
read_gpi() { busybox devmem 0xFF706014 32; }
write_gpo() { busybox devmem 0xFF706010 32 "$1"; }
initial=$(read_gpi)
signature=$(((initial >> 16) & 65535))
check() {
    status=$(read_gpi)
    [ "$(((status >> 16) & 65535))" -eq "$signature" ] || exit 1
    actual=$((status & $2))
    [ "$actual" -eq "$1" ] || {
        echo "FAIL: expected=$1 actual=$actual status=$status" >&2
        exit 1
    }
    echo "PASS: expected=$1 actual=$actual status=$status"
}
case "$signature" in
    54803) # 0xD613: 430_dsp_mul27, low 16 result bits
        for vector in '10 12' '255 255' '65537 3'; do
            set -- $vector
            write_gpo "$((($2 << 20) | $1))"
            sleep 0.02
            check "$((($1 * $2) & 65535))" 65535
        done
        ;;
    54805) # 0xD615: 450_dsp_mac, low 16 result bits
        for vector in '10 12 5' '255 255 7' '0 0 65535' '1 1 257'; do
            set -- $vector
            write_gpo "$((($3 << 16) | ($2 << 8) | $1))"
            sleep 0.02
            check "$((($1 * $2 + $3) & 65535))" 65535
        done
        ;;
    54806) # 0xD616: 460_dsp_reg, selected result byte
        for vector in '10 12' '255 255' '1 1'; do
            set -- $vector
            command=$((($2 << 8) | $1))
            write_gpo "$command"
            sleep 0.02
            check "$((($1 * $2) & 255))" 255
            write_gpo "$((command | 65536))"
            sleep 0.02
            check "$((($1 * $2) >> 8))" 255
        done
        # ENA is active when GPO bit 31 is low; hold 120 while changing inputs.
        write_gpo 3082
        sleep 0.02
        check 120 255
        write_gpo 2147486730
        sleep 0.02
        write_gpo 2147483905
        sleep 0.02
        check 120 255
        write_gpo 257
        sleep 0.02
        check 1 255
        ;;
    *) echo "Unsupported DSP diagnostic signature: $initial" >&2; exit 1 ;;
esac
