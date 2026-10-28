# Makefile - VERA card on the C64 expansion port: screen driver, tests and
# RMT player, collected on a D64 disk image.
#
#   make                  build everything and vera-c64.d64
#   make NO_IMAGE=        also tests/demo.vbm.prg (does not fit on the D64)
#   make run              start VICE with the card and the disk
#   make clean

SUBDIRS   = driver tests rmt
D64       = vera-c64.d64
D64_NAME  = vera c64,vc
C1541    ?= c1541
VICE     ?= x64sc

# the VERA always at $DF00 (IO2): IO1 is the GeoRAM window of the card
VERA_BASE ?= 0xDF00
export VERA_BASE

.PHONY: all clean run $(SUBDIRS)

all: $(D64)

$(SUBDIRS):
	$(MAKE) -C $@

# every PRG of the subdirectories, under its own name (lower case: PETSCII
# upper case on the C64 screen); not tests/pacman.prg and its loader, made of
# the arcade ROMs (tests/pacman.d64)
$(D64): $(SUBDIRS)
	rm -f $@
	$(C1541) -format "$(D64_NAME)" d64 $@ \
	    $(foreach p,$(filter-out tests/pacman.prg tests/pacman_loader.prg,$(sort $(wildcard $(addsuffix /*.prg,$(SUBDIRS))))),-write $(p) $(basename $(notdir $(p)))) > /dev/null
	$(C1541) -attach $@ -list

run: $(D64)
	tools/run-vice.sh $(D64)

clean:
	for d in $(SUBDIRS); do $(MAKE) -C $$d clean; done
	rm -f $(D64)
