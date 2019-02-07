NPNR = ../../nextpnr-ice40
JSON = $(wildcard */*.json)
OUTPUT = $(patsubst %.json,%.log,$(JSON))

all: $(NPNR) $(OUTPUT)

%.log: %.json $(wildcard %.pcf)
	$(NPNR) --json $(word 1,$^) $(if $(word 2,$^), --pcf $(word 2,$^),) > $@ 2>&1
