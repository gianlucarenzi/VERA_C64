# Makefile - VERA screen driver for the C64.
#
#   vera80.prg  80x30, 8x16 font
#   vera60.prg  80x60, 8x8 font
#   vera40.prg  40x30, 8x8 font scaled 2x (320x240)
#
#   make                  the card at $DF00 (IO2: IO1 is its GeoRAM window)

CA65      = ca65
LD65      = ld65
PYTHON   ?= python3
VERA_BASE ?= 0xDF00

# ca65 wants $DF00, make passes 0xDF00
BASE_ASM  = '$$$(patsubst 0x%,%,$(patsubst 0X%,%,$(VERA_BASE)))'
ASFLAGS   = --cpu 6502 -I ../include -I . -D VERA_BASE=$(BASE_ASM)

PRGS      = vera80.prg vera60.prg vera40.prg

all: $(PRGS)

gen/petscii8.bin: font/petscii.s mkfont.py
	@mkdir -p gen
	$(PYTHON) mkfont.py $< $@ --height 8

gen/petscii16.bin: font/petscii.s mkfont.py
	@mkdir -p gen
	$(PYTHON) mkfont.py $< $@ --height 16

# the base address is part of every object: rebuilt when it changes
gen/base.cfg: FORCE
	@mkdir -p gen
	@echo '$(VERA_BASE)' | cmp -s - $@ || echo '$(VERA_BASE)' > $@

DEPS = vera_screen.s ../include/vera_common.inc gen/base.cfg

gen/vera80.o: $(DEPS) gen/petscii16.bin
	$(CA65) $(ASFLAGS) -o $@ $<
gen/vera60.o: $(DEPS) gen/petscii8.bin
	$(CA65) $(ASFLAGS) -D FONT_8X8=1 -o $@ $<
gen/vera40.o: $(DEPS) gen/petscii8.bin
	$(CA65) $(ASFLAGS) -D FONT_8X8=1 -D MODE_40X30=1 -o $@ $<
gen/loadaddr.o: loadaddr.s gen/base.cfg
	$(CA65) $(ASFLAGS) -o $@ $<

vera%.prg: gen/vera%.o gen/loadaddr.o vera_screen.cfg
	$(LD65) -C vera_screen.cfg -m gen/vera$*.map -o $@ gen/loadaddr.o $<

clean:
	rm -rf gen $(PRGS)

.PHONY: all clean FORCE
