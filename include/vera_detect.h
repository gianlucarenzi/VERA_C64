#ifndef VERA_DETECT_H
#define VERA_DETECT_H

#include <stdio.h>
#include <stdlib.h>

#include "vera.h"

/*
 * Identifier of the VERA card, returned by vera_detect() on success:
 * 'V' (0x56) | 'X' (0x58), as on the Atari PBI card.
 */
#define VERA_CARD_ID  ((unsigned int)0x5658)

/*
 * vera_detect() - probe the VERA card at VERA_BASE ($DE00).
 *
 * Selects DCSEL=63 (CTRL=$7E) and checks the identity bytes: $09 = 'V',
 * $0A = major version >= 47 (the FX firmware generation: 47.0.2 reads
 * 'V',47,0,0, 48.0.1 reads 'V',48,0,1).  An open bus in IO1 reads what the
 * VIC-II fetched last, so a write/read-back probe is not enough; the
 * identity bytes are.  CTRL is restored to 0 afterwards.
 *
 * Returns VERA_CARD_ID when the card responds, 0 otherwise (card absent,
 * still configuring after power-on, or VICE started without -vera).
 */
static unsigned int vera_detect(void)
{
    unsigned char v, m;

    VERA_CTRL = 0x7E;
    v = VERA_REG09;
    m = VERA_REG0A;
    VERA_CTRL = 0x00;
    if (v != 'V' || m < 47) return 0;

    /* Restore the VRAM address registers to a safe state */
    VERA_ADDR_L = 0x00;
    VERA_ADDR_M = 0x00;
    VERA_ADDR_H = 0x00;

    return VERA_CARD_ID;
}

/*
 * vera_require() - fatal guard for programs that need the VERA card.
 *
 * Prints an error and calls exit(1) when the card is absent.
 * Returns VERA_CARD_ID on success.
 */
static unsigned int vera_require(void)
{
    unsigned int id = vera_detect();
    if (!id) {
        printf("error: vera card not found at $%04x.\n", (unsigned int)VERA_BASE);
        printf("vice: x64sc -vera\n");
        exit(1);
    }
    return id;
}

#endif /* VERA_DETECT_H */
