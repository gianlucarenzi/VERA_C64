#ifndef DETECT_H
#define DETECT_H

/*
 * detect.h - include/vera_detect.h for the cc65 c64 target.
 *
 * vera_detect() compares the identity byte with the character 'V', which
 * the c64 character map turns into PETSCII $D6: the card reads $56.  The
 * map of 'V' is the identity while the header is compiled.
 */
/* clang-format off */
#pragma charmap (0x56, 0x56)
#include "vera_detect.h"
#pragma charmap (0x56, 0xD6)
/* clang-format on */

#endif /* DETECT_H */
