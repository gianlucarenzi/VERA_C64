/* test_mem.c -- the 256 KB expansion RAM on a VERA card, next to the VERA itself.
 * The same file is in VERA_C64/tests and VERA_ATARI_PBI/vera-tests: keep them equal.
 *
 *   cl65 -t c64   -O -o testmem.prg test_mem.c  VERA cart (VICE): GeoRAM 256 KB,
 *                                                window $DE00, regs $DFFE/$DFFF;
 *                                                VERA at $DF00 (only there has the
 *                                                card its GeoRAM)
 *   cl65 -t atari -O -o TESTMEM.COM test_mem.c  VeraX16 PBI (atari800): RAMbo 256K,
 *                                                16 KB banks at $4000 via PORTB
 *                                                (the program must stay below
 *                                                $4000), VERA at $D100 while
 *                                                $D1FF = $80
 *
 * 16 banks x 64 pages x 256 bytes are filled and verified, writing and reading back
 * VERA registers after every page, then a VRAM write/read through DATA0.  Every byte
 * tested also goes straight into the border colour register; at the end the border
 * shows the result (green = ok, red = error). */
#include <conio.h>

#define BANKS 16
#define PAGES 64 /* of 256 bytes per bank */

#if defined(__C64__)
#define GEO_PAGE  (*(volatile unsigned char *)0xDFFE)
#define GEO_BLOCK (*(volatile unsigned char *)0xDFFF)
#define VERA      ((volatile unsigned char *)0xDF00)
#define BORDER    (*(volatile unsigned char *)0xD020)
#define NAME      "GEORAM 256K + VERA $DF00"
#define OK_COLOR  5 /* green */
#define ERR_COLOR 2 /* red */
static volatile unsigned char *page_select(unsigned char b, unsigned char p)
{
    GEO_BLOCK = b;
    GEO_PAGE = p;
    return (volatile unsigned char *)0xDE00;
}
#define CARDSEL    (*(volatile unsigned char *)0xDFFD) /* VERA select latch */
#define VERA_ON()  (CARDSEL = 1)                       /* the GeoRAM is always on */
#define VERA_OFF() (CARDSEL = 0)
#define BANKING_BEGIN()
#define BANKING_END()
#define SHOW_RESULT(c) (BORDER = (c))

#elif defined(__ATARI__)
#define PORTB           (*(volatile unsigned char *)0xD301)
#define PBISEL          (*(volatile unsigned char *)0xD1FF)
#define VERA            ((volatile unsigned char *)0xD100)
#define BORDER          (*(volatile unsigned char *)0xD01A) /* COLBK: the border in GR.0 */
#define COLOR4          (*(volatile unsigned char *)0x02C8) /* its shadow, reloaded by the VBI */
#define NAME            "RAMBO 256K + VERA PBI"
#define OK_COLOR        0xC6
#define ERR_COLOR       0x36
static unsigned char portb_save;
static volatile unsigned char *page_select(unsigned char b, unsigned char p)
{
    PORTB = 0x83 | ((b & 3) << 2) | (((b >> 2) & 3) << 5); /* bit 4 = 0: bank to the CPU */
    return (volatile unsigned char *)(0x4000 + ((unsigned)p << 8));
}
#define VERA_ON()       (PBISEL = 0x80) /* select the card */
#define VERA_OFF()      (PBISEL = 0)
#define BANKING_BEGIN() (portb_save = PORTB, __asm__("sei"))
#define BANKING_END()   (PORTB = portb_save, __asm__("cli"))
#define SHOW_RESULT(c)  (COLOR4 = BORDER = (c))
#else
#error "build with -t c64 or -t atari"
#endif

#define PATTERN(b, p, i) ((unsigned char)((b) ^ ((p)*3) ^ (i)))

int main(void)
{
    volatile unsigned char *win;
    unsigned char b, p, i, bad_vreg = 0, bad_vram = 0;
    unsigned int n, bad_ram = 0;

    clrscr();
    cputs(NAME " TEST\r\n");

    BANKING_BEGIN();
    for (b = 0; b < BANKS; ++b)
        for (p = 0; p < PAGES; ++p)
        {
            win = page_select(b, p);
            i = 0;
            do
            {
                BORDER = win[i] = PATTERN(b, p, i);
            } while (++i);
            VERA_ON();
            VERA[0] = p; /* ADDR0_L / ADDR0_M */
            VERA[1] = b;
            if (VERA[0] != p || VERA[1] != b) ++bad_vreg;
            VERA_OFF();
        }
#if defined(__ATARI__)
    PORTB = portb_save | 0x10;   /* main RAM in the window: */
    for (n = 0; n < 0x4000; ++n) /* must stay apart from the banks */
        BORDER = ((volatile unsigned char *)0x4000)[n] = (unsigned char)(n ^ 0xA5);
#endif
    for (b = 0; b < BANKS; ++b)
        for (p = 0; p < PAGES; ++p)
        {
            win = page_select(b, p);
            i = 0;
            do
            {
                if ((BORDER = win[i]) != PATTERN(b, p, i)) ++bad_ram;
            } while (++i);
        }
#if defined(__ATARI__)
    PORTB = portb_save | 0x10;
    for (n = 0; n < 0x4000; ++n)
        if ((BORDER = ((volatile unsigned char *)0x4000)[n]) != (unsigned char)(n ^ 0xA5)) ++bad_ram;
#endif
    BANKING_END();

    VERA_ON();
    VERA[5] = 0; /* CTRL: ADDRSEL 0 */
    VERA[0] = 0;
    VERA[1] = 0;
    VERA[2] = 0x10; /* $00000, increment 1 */
    for (n = 0; n < 256; ++n) VERA[3] = (unsigned char)(n * 7);
    VERA[0] = 0;
    VERA[1] = 0;
    VERA[2] = 0x10;
    for (n = 0; n < 256; ++n)
        if (VERA[3] != (unsigned char)(n * 7)) ++bad_vram;
    VERA_OFF();

    cprintf("RAM 256K:  %s\r\n", bad_ram ? "ERROR" : "OK");
    cprintf("VERA REGS: %s\r\n", bad_vreg ? "ERROR" : "OK");
    cprintf("VERA VRAM: %s\r\n", bad_vram ? "ERROR" : "OK");
    SHOW_RESULT((bad_ram | bad_vreg | bad_vram) ? ERR_COLOR : OK_COLOR);
    for (;;)
        ;
}
