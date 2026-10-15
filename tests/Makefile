# Makefile - VERA test programs for the C64 (card at VERA_BASE, $DE00 by
# default; VERA_BASE=0xDF00 for IO2).

CL65      = cl65
CA65      = ca65
PYTHON   ?= python3
VERA_BASE ?= 0xDE00

INC       = ../include
CFLAGS    = -t c64 -O -I $(INC) -DVERA_BASE=$(VERA_BASE)u
ASFLAGS   = -t c64 -I $(INC) -I . -D 'VERA_BASE=$(subst 0x,$$,$(VERA_BASE))'
HEADERS   = $(INC)/vera.h $(INC)/vera_detect.h detect.h

# the text tests in the three modes of vtext.c: 4 = 40x30, 8 = 80x30, 6 = 80x60
MODES     = 4 8 6
TEXT_TESTS = testfont testgs testmaze testmtx
TEXT_PRGS = $(foreach t,$(TEXT_TESTS),$(foreach m,$(MODES),$(t)$(m).prg))

# the VTM player and its data files (PRG files on the disk: the top
# Makefile writes every *.prg under its name without ".prg")
DEMO_SONG_SRC ?= songs/axelf.vtms
DEMO_IMAGE    ?= songs/DEMO.VBM
# demo.vbm.prg (305 blocks) does not fit on the D64 with the other PRGs:
# left out by default, NO_IMAGE= builds it
NO_IMAGE      ?= 1
PLAYER_DATA   = demo.vtm.prg $(if $(NO_IMAGE),,demo.vbm.prg)

PRGS = testfx.prg testirq.prg $(TEXT_PRGS) testplr.prg $(PLAYER_DATA) vic_mirror.prg

.PHONY: all clean
all: $(PRGS)

%.o: %.s $(INC)/vera_common.inc
	$(CA65) $(ASFLAGS) -o $@ $<

testfx.prg: test_fx.c $(HEADERS)
	$(CL65) $(CFLAGS) -o $@ $<

testirq.prg: test_irq.c vera_irq.o vera_irq.h $(HEADERS)
	$(CL65) $(CFLAGS) -o $@ test_irq.c vera_irq.o

# $(1) = program base name, $(2) = C source, $(3) = mode
define text_test
$(1)$(3).prg: $(2) vtext.c vtext.h vtfont.s ../fonts/font8x8.bin ../fonts/font8x16.bin $$(HEADERS)
	$$(CL65) $$(CFLAGS) -DVT_MODE=$(3) --asm-define VT_MODE=$(3) --bin-include-dir ../fonts -o $$@ $(2) vtext.c vtfont.s
endef

$(foreach m,$(MODES),$(eval $(call text_test,testfont,test_font.c,$(m))))
$(foreach m,$(MODES),$(eval $(call text_test,testgs,test_gradient_scroll.c,$(m))))
$(foreach m,$(MODES),$(eval $(call text_test,testmaze,test_maze.c,$(m))))
$(foreach m,$(MODES),$(eval $(call text_test,testmtx,test_matrix.c,$(m))))

testplr.prg: test_player.c vtm_loader.c vbm_loader.c vtm_player.o vbm_display.o vtm.h vbm.h c64-zp.cfg $(HEADERS)
	$(CL65) $(CFLAGS) -C c64-zp.cfg -o $@ test_player.c vtm_loader.c vbm_loader.c vtm_player.o vbm_display.o

vic_mirror.prg: vic_mirror.c $(HEADERS)
	$(CL65) $(CFLAGS) -o $@ $<

vtm_player.o: vtm_notes.inc

demo.vtm.prg: $(DEMO_SONG_SRC) tools/vtm_compile.py
	$(PYTHON) tools/vtm_compile.py $< $@

demo.vbm.prg: $(DEMO_IMAGE)
	cp $< $@

clean:
	rm -f *.prg *.o *.map
