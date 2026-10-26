/*
 * vera.h - the registers of the VERA card on the C64 expansion port.
 *
 * The card decodes A0-A4 and /IO1 (or /IO2): its 32 registers are at $DE00,
 * or at $DF00 when built with -DVERA_BASE=0xDF00 (see vera_common.inc).
 */

#ifndef VERA_H
#define VERA_H

#ifndef VERA_BASE
#define VERA_BASE 0xDE00u
#endif

#define VERA_REG(n)     (*(volatile unsigned char *)(VERA_BASE + (n)))

/* The select latch of the card (RBL-C64, and VICE): write != 0 to $DFFD
 * selects the VERA, 0 deselects it; its registers answer only while it is
 * selected, at either base.  Cleared at power-on, kept across a reset.  The
 * GeoRAM of the card is always active.  vera_detect() selects the VERA. */
#define VERA_CARDSEL    (*(volatile unsigned char *)0xDFFD)
#define VERA_SELECT()   (VERA_CARDSEL = 1)
#define VERA_DESELECT() (VERA_CARDSEL = 0)

#define VERA_ADDR_L     VERA_REG(0x00)
#define VERA_ADDR_M     VERA_REG(0x01)
#define VERA_ADDR_H     VERA_REG(0x02)
#define VERA_DATA0      VERA_REG(0x03)
#define VERA_DATA1      VERA_REG(0x04)
#define VERA_CTRL       VERA_REG(0x05)
#define VERA_IEN        VERA_REG(0x06)
#define VERA_ISR        VERA_REG(0x07)
#define VERA_IRQLINE_L  VERA_REG(0x08)  /* write: LINE IRQ line bits 7:0 */
#define VERA_SCANLINE_L VERA_REG(0x08)  /* read: current line bits 7:0 */

/* DCSEL-muxed registers at $09-$0C */
#define VERA_REG09      VERA_REG(0x09)
#define VERA_REG0A      VERA_REG(0x0A)
#define VERA_REG0B      VERA_REG(0x0B)
#define VERA_REG0C      VERA_REG(0x0C)
#define VERA_DC_VIDEO   VERA_REG(0x09)  /* DCSEL 0 */
#define VERA_DC_HSCALE  VERA_REG(0x0A)
#define VERA_DC_VSCALE  VERA_REG(0x0B)
#define VERA_DC_BORDER  VERA_REG(0x0C)
#define VERA_DC_HSTART  VERA_REG(0x09)  /* DCSEL 1 */
#define VERA_DC_HSTOP   VERA_REG(0x0A)
#define VERA_DC_VSTART  VERA_REG(0x0B)
#define VERA_DC_VSTOP   VERA_REG(0x0C)

#define VERA_L0_CONFIG   VERA_REG(0x0D)
#define VERA_L0_MAPBASE  VERA_REG(0x0E)
#define VERA_L0_TILEBASE VERA_REG(0x0F)
#define VERA_L0_HSCR_L   VERA_REG(0x10)
#define VERA_L0_HSCR_H   VERA_REG(0x11)
#define VERA_L0_VSCR_L   VERA_REG(0x12)
#define VERA_L0_VSCR_H   VERA_REG(0x13)
#define VERA_L1_CONFIG   VERA_REG(0x14)
#define VERA_L1_MAPBASE  VERA_REG(0x15)
#define VERA_L1_TILEBASE VERA_REG(0x16)
#define VERA_L1_HSCR_L   VERA_REG(0x17)
#define VERA_L1_HSCR_H   VERA_REG(0x18)
#define VERA_L1_VSCR_L   VERA_REG(0x19)
#define VERA_L1_VSCR_H   VERA_REG(0x1A)

#define VERA_AUDIO_CTRL VERA_REG(0x1B)
#define VERA_AUDIO_RATE VERA_REG(0x1C)
#define VERA_AUDIO_DATA VERA_REG(0x1D)
#define VERA_SPI_DATA   VERA_REG(0x1E)
#define VERA_SPI_CTRL   VERA_REG(0x1F)

/* VRAM address with auto-increment: ADDR_H = incr | bank */
#define VERA_INC0       0x00
#define VERA_INC1       0x10
#define VERA_INC2       0x20
#define VERA_INC4       0x30

#define VERA_PSG_BASE     0x1F9C0ul     /* 16 voices x 4 bytes */
#define VERA_PALETTE_BASE 0x1FA00ul     /* 256 x 2 bytes, GGGGBBBB 0000RRRR */
#define VERA_SPRITE_BASE  0x1FC00ul     /* 128 x 8 bytes */

/* Point data port 0 at a VRAM address with the given increment. */
#define VERA_SET_ADDR(addr, inc) do { \
        VERA_CTRL = 0; \
        VERA_ADDR_L = (unsigned char)(addr); \
        VERA_ADDR_M = (unsigned char)((addr) >> 8); \
        VERA_ADDR_H = (unsigned char)((inc) | (((addr) >> 16) & 1)); \
    } while (0)

#endif /* VERA_H */
