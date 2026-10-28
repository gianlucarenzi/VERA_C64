# Makefile - VERA test programs for the C64 (card at VERA_BASE, $DE00 by
# default; VERA_BASE=0xDF00 for IO2).

CL65      = cl65
CA65      = ca65
LD65      = ld65
C1541    ?= c1541
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

# Pac-Man (test_pacman.c) with the graphics and the sounds of the arcade, from
# the MAME ROM set "pacman" (not in the repository): built only when
# PACMAN_ZIP is there.  The sounds are ripped by running the game in MAME
# (pacman_rip.lua, about a minute), and so are the attract mode and the
# intermissions with their song (pacman_scenes.lua, a few seconds); the logs
# in gen/ are kept, so MAME is needed only once.  pacman.prg stays off the
# D64 (no room, and the ROM data is not ours to hand out).  Joystick in port 2; in VICE also on the cursor
# keys and SPACE: JOYKEYS=1 tools/run-vice.sh tests/pacman.d64 (with the
# loader) or tests/pacman.prg
PACMAN_ZIP ?= $(HOME)/Downloads/pacman.zip
MAME       ?= mame
PACMAN     = $(and $(wildcard $(PACMAN_ZIP)),$(or $(and $(wildcard gen/pacman_rip.log),$(wildcard gen/pacman_scenes.log)),$(shell command -v $(MAME))),pacman.prg)

PRGS = testfx.prg testirq.prg $(TEXT_PRGS) testplr.prg $(PLAYER_DATA) vic_mirror.prg testmem.prg \
       $(if $(PACMAN),$(PACMAN) pacman_loader.prg pacman.d64)

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

# the card's GeoRAM next to the VERA: always for $DF00, the only base where the card
# has its GeoRAM (test_mem.c is shared with VERA_ATARI_PBI/vera-tests)
testmem.prg: test_mem.c
	$(CL65) -t c64 -O -o $@ $<

vtm_player.o: vtm_notes.inc

demo.vtm.prg: $(DEMO_SONG_SRC) tools/vtm_compile.py
	$(PYTHON) tools/vtm_compile.py $< $@

demo.vbm.prg: $(DEMO_IMAGE)
	cp $< $@

pacman.prg: test_pacman.c gen/pacgfx.s gen/pacgfx.h gen/pacsnd.s gen/pacsnd.h gen/pacscn.s gen/pacscn.h $(HEADERS)
	$(CL65) $(CFLAGS) -Oirs -o $@ test_pacman.c gen/pacgfx.s gen/pacsnd.s gen/pacscn.s

gen/pacgfx.s: pacman_gfx.py $(PACMAN_ZIP)
	mkdir -p gen
	$(PYTHON) pacman_gfx.py $(PACMAN_ZIP) $@ gen/pacgfx.h
gen/pacgfx.h: gen/pacgfx.s ;

# MAME looks for the set by name: PACMAN_ZIP must be called pacman.zip
gen/pacman_rip.log: pacman_rip.lua $(PACMAN_ZIP)
	mkdir -p gen
	PACRIP_OUT=$@ $(MAME) pacman -rompath $(dir $(PACMAN_ZIP)) -video none -sound none \
	    -nothrottle -skip_gameinfo -seconds_to_run 120 -autoboot_script pacman_rip.lua

gen/pacman_scenes.log: pacman_scenes.lua $(PACMAN_ZIP)
	mkdir -p gen
	PACSCN_OUT=$@ $(MAME) pacman -rompath $(dir $(PACMAN_ZIP)) -video none -sound none \
	    -nothrottle -skip_gameinfo -seconds_to_run 200 -autoboot_script pacman_scenes.lua

gen/pacsnd.s: pacman_snd.py gen/pacman_rip.log gen/pacman_scenes.log
	$(PYTHON) pacman_snd.py gen/pacman_rip.log gen/pacman_scenes.log $@ gen/pacsnd.h
gen/pacsnd.h: gen/pacsnd.s ;

gen/pacscn.s: pacman_scene.py pacman_gfx.py gen/pacman_scenes.log
	$(PYTHON) pacman_scene.py gen/pacman_scenes.log $@ gen/pacscn.h
gen/pacscn.h: gen/pacscn.s ;

# pacman.d64: the loader "pacman" first (LOAD"*",8 and VICE autostart take
# it), then the program as "pacman data".  The loader shows a progress bar
# on the C64 screen, in the font of the arcade (pacman_font.py), and keeps
# the VERA black while it reads the program; the size of pacman.prg sets its
# thresholds.
pacman_loader.prg: pacman_loader.s pacman_loader.cfg gen/pacfont.inc pacman.prg $(INC)/vera_common.inc
	$(CA65) $(ASFLAGS) -D MAIN_BYTES=$$(stat -c %s pacman.prg) -o pacman_loader.o pacman_loader.s
	$(LD65) -C pacman_loader.cfg -o $@ pacman_loader.o

gen/pacfont.inc: pacman_font.py pacman_gfx.py $(PACMAN_ZIP)
	mkdir -p gen
	$(PYTHON) pacman_font.py $(PACMAN_ZIP) $@

pacman.d64: pacman_loader.prg pacman.prg
	rm -f $@
	$(C1541) -format "pac-man,pm" d64 $@ -write pacman_loader.prg pacman -write pacman.prg "pacman data" > /dev/null

clean:
	rm -f *.prg *.o *.map pacman.d64 gen/pacgfx.* gen/pacsnd.* gen/pacscn.* gen/pacfont.*
