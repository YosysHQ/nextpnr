SHELL = /bin/bash

reports::

define mkreport
reports:: report_a$1.txt report_n$1.txt

report_a$1.txt: hx8kdemo_a$1.asc
	icetime -m -r report_a$1.txt -d hx8k hx8kdemo_a$1.asc

report_n$1.txt: hx8kdemo_n$1.asc
	icetime -m -r report_n$1.txt -d hx8k hx8kdemo_n$1.asc

hx8kdemo_a$1.asc: hx8kdemo.blif
	{ time arachne-pnr -d 8k -p hx8kdemo.pcf -o hx8kdemo_a$1.asc -s 1$1 hx8kdemo.blif; } > hx8kdemo_a$1.log 2>&1

hx8kdemo_n$1.asc: hx8kdemo.json
	{ time ../../nextpnr-ice40 --asc hx8kdemo_n$1.asc --json hx8kdemo.json --pcf hx8kdemo.pcf --hx8k --seed 1$1; } > hx8kdemo_n$1.log 2>&1
endef

$(foreach i,0 1 2 3 4 5 6 7 8 9,$(eval $(call mkreport,$(i))))

hx8kdemo.blif: hx8kdemo.json
hx8kdemo.json: hx8kdemo.v spimemio.v simpleuart.v picosoc.v picorv32.v
	yosys -ql hx8kdemo.log -p 'synth_ice40 -top hx8kdemo -blif hx8kdemo.blif -json hx8kdemo.json' $^

clean:
	rm -f hx8kdemo.log hx8kdemo.blif hx8kdemo.json
	rm -f hx8kdemo_[an][0-9].asc hx8kdemo_[an][0-9].log report_[an][0-9].txt
