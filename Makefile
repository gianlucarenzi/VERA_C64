# Makefile - VERA card on the C64 expansion port: screen driver, tests and
# RMT player, collected on a D64 disk image.
#
#   make                  build everything and vera-c64.d64
#   make VERA_BASE=0xDF00 the card in IO2 instead of IO1
#   make run              start VICE with the card and the disk
#   make clean

SUBDIRS   = driver tests rmt
D64       = vera-c64.d64
D64_NAME  = vera c64,vc
C1541    ?= c1541
VICE     ?= x64sc

VERA_BASE ?= 0xDE00
export VERA_BASE

.PHONY: all clean run $(SUBDIRS)

all: $(D64)

$(SUBDIRS):
	$(MAKE) -C $@

# every PRG of the subdirectories, under its own name (lower case: PETSCII
# upper case on the C64 screen)
$(D64): $(SUBDIRS)
	rm -f $@
	$(C1541) -format "$(D64_NAME)" d64 $@ \
	    $(foreach p,$(sort $(wildcard $(addsuffix /*.prg,$(SUBDIRS)))),-write $(p) $(basename $(notdir $(p)))) > /dev/null
	$(C1541) -attach $@ -list

run: $(D64)
	tools/run-vice.sh $(D64)

clean:
	for d in $(SUBDIRS); do $(MAKE) -C $$d clean; done
	rm -f $(D64)
