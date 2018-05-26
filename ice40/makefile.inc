archs += ice40
ice40_objs = chip.o main.o

define ice40_chipdb
ice40_objs += chipdb-$(1).o
ice40/chipdb-$(1).cc: ice40/chipdb.py /usr/local/share/icebox/chipdb-$(1).txt
	python3 ice40/chipdb.py /usr/local/share/icebox/chipdb-$(1).txt > ice40/chipdb-$(1).cc.new
	mv ice40/chipdb-$(1).cc.new ice40/chipdb-$(1).cc

clean::
	rm -f ice40/chipdb-$(1).cc
endef

$(eval $(call ice40_chipdb,384))
$(eval $(call ice40_chipdb,1k))
$(eval $(call ice40_chipdb,5k))
$(eval $(call ice40_chipdb,8k))
