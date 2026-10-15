# rmt/Makefile - RMT player on the VERA PSG of the C64 VERA card
#
#   make                                     testrmt.prg and rmtplay.prg
#   make RMT_SONG=music/turrican2_rev2s.rmt  another module: RMT4 or RMT8
#   make RMT_SONG=music/gemx.sap             a SAP or XEX exported by RITMO /
#   make RMT_SONG=x.xex RMT_SUBSONG=1        RMT: its LZSS streams (lzsplayr.s)
#   make RMT_NAME=".." RMT_AUTHOR=".." RMT_DATE=..
#                                            text of rmtplay.prg (default: the
#                                            text stored in the song file)
#   make RMT_SCREEN=vera|vic|both            screens of rmtplay.prg (both)
#   make VERA_BASE=0xDF00                    the card in IO2
#   make clean
#
# The player is the Atari RMT routine (rmtplayr.s, from VERA_ATARI_PBI
# vera-tests/rmt and AT2019/ATARI-Driver/rmt_cbm64) assembled with
# -D RMT_C64 -D RMT_VERA: it writes the POKEY registers to RAM and psgrmt.s
# plays them on the VERA PSG; rmtirq.s calls it from a raster IRQ.

CL       ?= cl65
PYTHON   ?= python3

GEN      = gen
INC      = ../include

VERA_BASE ?= 0xDE00
# ca65 takes $DE00, cc65 0xDE00
VERA_BASE_ASM = $$$(patsubst 0x%,%,$(patsubst 0X%,%,$(VERA_BASE)))

RMT_SONG    ?= music/gemx.rmt
RMT_SUBSONG ?= 0
RMT_NAME    ?=
RMT_AUTHOR  ?=
RMT_DATE    ?=
RMT_SCREEN  ?= both
RMT_SONGNAME = $(basename $(notdir $(RMT_SONG)))

# a .sap or .xex exported by RITMO / RMT: the VU-Player LZSS streams
ifneq ($(filter .sap .SAP .xex .XEX,$(suffix $(RMT_SONG))),)
RMT_LZSS    = 1
RMT_PLAYER  = lzsplayr.s
RMT_TRACKS := $(shell $(PYTHON) tools/sap2ca65.py $(RMT_SONG) --tracks)
else
RMT_PLAYER  = rmtplayr.s
RMT_TRACKS := $(shell $(PYTHON) -c "d=open('$(RMT_SONG)','rb').read(10); print(8 if d[6:10]==b'RMT8' else 4)")
endif

ifeq ($(RMT_SCREEN),vera)
SCREEN_FLAGS = -DRMTPLAY_VIC=0
else ifeq ($(RMT_SCREEN),vic)
SCREEN_FLAGS = -DRMTPLAY_VERA=0
else ifeq ($(RMT_SCREEN),both)
SCREEN_FLAGS =
else
$(error RMT_SCREEN must be vera, vic or both, not '$(RMT_SCREEN)')
endif

ASFLAGS = -t c64 --asm-include-dir $(INC) --asm-include-dir . \
          --asm-define RMT_C64 --asm-define RMT_VERA \
          --asm-define RMT_TRACKS=$(RMT_TRACKS) --asm-define 'VERA_BASE=$(VERA_BASE_ASM)'
CFLAGS  = -t c64 -Osir -I $(INC) -I . -I $(GEN) -DVERA_BASE=$(VERA_BASE) \
          -DRMT_TRACKS=$(RMT_TRACKS) $(if $(RMT_LZSS),-DRMT_LZSS)

TESTRMT = testrmt.prg
RMTPLAY = rmtplay.prg

all: $(TESTRMT) $(RMTPLAY)

# options kept in stamp files: changing them rebuilds what depends on them
$(GEN)/build.cfg: FORCE
	@mkdir -p $(GEN)
	@echo '$(RMT_SONG) $(RMT_SUBSONG) $(RMT_TRACKS) $(VERA_BASE)' | cmp -s - $@ || \
		echo '$(RMT_SONG) $(RMT_SUBSONG) $(RMT_TRACKS) $(VERA_BASE)' > $@

$(GEN)/songinfo.cfg: FORCE
	@mkdir -p $(GEN)
	@printf '%s\n' '$(RMT_SONG)|$(RMT_SUBSONG)|$(RMT_NAME)|$(RMT_AUTHOR)|$(RMT_DATE)' | cmp -s - $@ || \
		printf '%s\n' '$(RMT_SONG)|$(RMT_SUBSONG)|$(RMT_NAME)|$(RMT_AUTHOR)|$(RMT_DATE)' > $@

$(GEN)/screen.cfg: FORCE
	@mkdir -p $(GEN)
	@echo '$(RMT_SCREEN)' | cmp -s - $@ || echo '$(RMT_SCREEN)' > $@

$(GEN)/psgtab.s: tools/mkpsgtab.py
	@mkdir -p $(GEN)
	$(PYTHON) $< $@

$(GEN)/song.s: $(RMT_SONG) $(GEN)/build.cfg tools/rmt2ca65.py tools/sap2ca65.py
ifdef RMT_LZSS
	$(PYTHON) tools/sap2ca65.py $(RMT_SONG) $@ --subsong $(RMT_SUBSONG)
else
	$(PYTHON) tools/rmt2ca65.py $(RMT_SONG) $@
endif

$(GEN)/songinfo.h: $(RMT_SONG) $(GEN)/songinfo.cfg tools/rmtinfo.py tools/sap2ca65.py
ifdef RMT_LZSS
	$(PYTHON) tools/sap2ca65.py $(RMT_SONG) /dev/null --subsong $(RMT_SUBSONG) --info $@ \
		--name "$(RMT_NAME)" --author "$(RMT_AUTHOR)" --date "$(RMT_DATE)"
else
	$(PYTHON) tools/rmtinfo.py $(RMT_SONG) $@ \
		--name "$(RMT_NAME)" --author "$(RMT_AUTHOR)" --date "$(RMT_DATE)"
endif

$(GEN)/player.o: $(RMT_PLAYER) rmt_feat.inc $(GEN)/build.cfg
	$(CL) -c $(ASFLAGS) -o $@ $<

$(GEN)/psgrmt.o: psgrmt.s $(INC)/vera_common.inc $(GEN)/build.cfg
	$(CL) -c $(ASFLAGS) -o $@ $<

$(GEN)/rmtirq.o: rmtirq.s $(GEN)/build.cfg
	$(CL) -c $(ASFLAGS) -o $@ $<

$(GEN)/psgtab.o: $(GEN)/psgtab.s
	$(CL) -c $(ASFLAGS) -o $@ $<

$(GEN)/song.o: $(GEN)/song.s
	$(CL) -c $(ASFLAGS) -o $@ $<

$(GEN)/test_rmt.o: test_rmt.c rmt.h $(INC)/vera.h $(INC)/vera_detect.h $(GEN)/build.cfg
	$(CL) -c $(CFLAGS) -DRMT_SONG_NAME=\"$(RMT_SONGNAME)\" -o $@ $<

$(GEN)/rmtplay.o: rmtplay.c rmt.h $(INC)/vera.h $(INC)/vera_detect.h $(GEN)/songinfo.h $(GEN)/build.cfg $(GEN)/screen.cfg
	$(CL) -c $(CFLAGS) $(SCREEN_FLAGS) -o $@ $<

PLAYER_OBJS = $(GEN)/player.o $(GEN)/psgrmt.o $(GEN)/rmtirq.o $(GEN)/psgtab.o $(GEN)/song.o

$(TESTRMT): $(GEN)/test_rmt.o $(PLAYER_OBJS) rmt.cfg
	$(CL) -t c64 -C rmt.cfg -m $(GEN)/testrmt.map -o $@ $(GEN)/test_rmt.o $(PLAYER_OBJS)

$(RMTPLAY): $(GEN)/rmtplay.o $(PLAYER_OBJS) rmt.cfg
	$(CL) -t c64 -C rmt.cfg -m $(GEN)/rmtplay.map -o $@ $(GEN)/rmtplay.o $(PLAYER_OBJS)

clean:
	rm -rf $(GEN) $(TESTRMT) $(RMTPLAY)

.PHONY: all clean FORCE
