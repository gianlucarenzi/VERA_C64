/* test_irq.c — VERA IRQ hook (CINV) test.
 *
 * Standalone program: output goes to the C64 screen.  Exercises vera_irq.s:
 *
 *  1. hook installed on CINV, previous vector kept for chaining
 *  2. VSYNC IRQ rate ~ 59.94 Hz, independent of the C64 frame rate
 *  3. LINE IRQ at raster line 240, once per VERA frame
 *  4. AUDIO FIFO LOW is masked after the first IRQ (cannot be acknowledged)
 *     and the machine keeps running (no IRQ storm)
 *  5. the KERNAL still gets the CIA 1 IRQs through the chain (the jiffy
 *     clock keeps running), and only them: a VERA IRQ passed on to the
 *     KERNAL would speed up the jiffy clock and break the counts of [2]
 *  6. vera_irq_remove() disables the sources and restores CINV
 *
 * Build: see Makefile (testirq.prg).
 */

#include <stdio.h>
#include <conio.h>
#include "detect.h"
#include "vera_irq.h"

#define CINV     (*(volatile unsigned int *)0x0314)
/* KERNAL jiffy clock TIME ($A0-$A2), low byte: 60 Hz on PAL and NTSC */
#define JIFFY    (*(volatile unsigned char *)0x00A2)

#define MEASURE_JIFFIES 120u   /* ~2 s */

static unsigned char g_pass = 0;
static unsigned char g_fail = 0;
static unsigned char g_lines = 0;
static const char *g_section = "";

static void pause_page(void)
{
    if (g_lines >= 20)
    {
        printf("-- press a key --\n");
        cgetc();
        g_lines = 0;
    }
}

static void check(const char *name, unsigned char ok, unsigned int got, unsigned int exp)
{
    if (ok)
    {
        g_pass++;
        return;
    }
    pause_page();
    printf("FAIL %s %s\n     got %u exp %u\n", g_section, name, got, exp);
    g_lines += 2;
    g_fail++;
}

/* Wait n jiffies (1/60 s) using the KERNAL clock. */
static void wait_jiffies(unsigned char n)
{
    unsigned char start = JIFFY;
    while ((unsigned char)(JIFFY - start) < n)
        ;
}

/* Expected VERA events in 'jiffies' KERNAL jiffies: the CIA 1 timer runs
 * at 60 Hz (PAL and NTSC), the VERA at 59.94 Hz fixed. */
static unsigned int expected_events(unsigned char jiffies)
{
    return (unsigned int)(((unsigned long)jiffies * 5994ul) / 6000ul);
}

static unsigned char near_enough(unsigned int got, unsigned int exp)
{
    unsigned int tol = exp / 20u + 1u;   /* 5 % + 1 */
    return got + tol >= exp && got <= exp + tol;
}

int main(void)
{
    unsigned char before, jiff0, jiff1, flags;
    unsigned int exp, got;
    unsigned int old_vec;

    putchar(14);            /* lower/upper case character set */
    vera_require();

    printf("VERA IRQ hook test\n");
    printf("==================\n");

    old_vec = CINV;

    /* Start from a known VERA state: all IRQs off, status clear */
    VERA_IEN = 0;
    VERA_ISR = 0x07;

    /* [1] install */
    g_section = "[1]";
    vera_irq_install();
    check("CINV changed", CINV != old_vec,
          CINV, old_vec);
    check("no IRQ enabled yet", (VERA_IEN & 0x0F) == 0, VERA_IEN & 0x0F, 0);

    /* [2] VSYNC */
    g_section = "[2] VSYNC";
    vera_irq_take();
    before = vera_irq_count[0];
    jiff0 = JIFFY;
    vera_irq_enable(VERA_IRQ_VSYNC);
    wait_jiffies(MEASURE_JIFFIES);
    vera_irq_disable(VERA_IRQ_VSYNC);
    jiff1 = JIFFY;
    got = (unsigned char)(vera_irq_count[0] - before);
    exp = expected_events((unsigned char)(jiff1 - jiff0));
    check("count ~ 59.94 Hz", near_enough(got, exp), got, exp);
    flags = vera_irq_take();
    check("flag VSYNC set", (flags & VERA_IRQ_VSYNC) != 0, flags, VERA_IRQ_VSYNC);
    check("IEN VSYNC off after disable", (VERA_IEN & VERA_IRQ_VSYNC) == 0, VERA_IEN & 1, 0);

    /* [3] LINE */
    g_section = "[3] LINE";
    vera_irq_set_line(240);
    vera_irq_take();
    before = vera_irq_count[1];
    jiff0 = JIFFY;
    vera_irq_enable(VERA_IRQ_LINE);
    wait_jiffies(MEASURE_JIFFIES);
    vera_irq_disable(VERA_IRQ_LINE);
    jiff1 = JIFFY;
    got = (unsigned char)(vera_irq_count[1] - before);
    exp = expected_events((unsigned char)(jiff1 - jiff0));
    check("count ~ one per VERA frame", near_enough(got, exp), got, exp);
    flags = vera_irq_take();
    check("flag LINE set", (flags & VERA_IRQ_LINE) != 0, flags, VERA_IRQ_LINE);

    /* [4] AFLOW: FIFO is empty after reset, so the IRQ is pending at once */
    g_section = "[4] AFLOW";
    before = vera_irq_count[3];
    jiff0 = JIFFY;
    vera_irq_enable(VERA_IRQ_AFLOW);
    wait_jiffies(6);
    got = (unsigned char)(vera_irq_count[3] - before);
    check("serviced exactly once (then masked)", got == 1, got, 1);
    check("IEN AFLOW masked by the hook", (VERA_IEN & VERA_IRQ_AFLOW) == 0, VERA_IEN & 8, 0);
    flags = vera_irq_take();
    check("flag AFLOW set", (flags & VERA_IRQ_AFLOW) != 0, flags, VERA_IRQ_AFLOW);

    /* [5] the OS clock kept running through the chain */
    g_section = "[5] chain";
    jiff1 = JIFFY;
    check("jiffy clock advanced during the test", (unsigned char)(jiff1 - jiff0) != 0, jiff1 - jiff0, 1);

    /* [6] remove */
    g_section = "[6] remove";
    vera_irq_remove();
    check("CINV restored", CINV == old_vec,
          CINV, old_vec);
    check("all sources disabled", (VERA_IEN & 0x0F) == 0, VERA_IEN & 0x0F, 0);

    printf("\n==================\n");
    printf("PASS: %d   FAIL: %d\n", (unsigned int)g_pass, (unsigned int)g_fail);
    printf("Press any key...\n");
    cgetc();
    return (g_fail == 0) ? 0 : 1;
}
