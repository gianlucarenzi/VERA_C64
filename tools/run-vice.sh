#!/bin/sh
# run-vice.sh - start VICE with the VERA card and a disk or a program.
#
#   tools/run-vice.sh vera-c64.d64          the disk in drive 8
#   tools/run-vice.sh tests/testfx.prg      autostart a program
#
# VICE:       VICE=x64sc-3.10 (default x64sc; it needs the VERA card, see
#             https://github.com/gianlucarenzi/vice, branch main)
# VERA_BASE:  0xDF00 (IO2, the card's own; IO1 is its GeoRAM window)
# SDCARD:     raw SD card image on the SPI port of the VERA
# JOYKEYS=1:  the joystick in port 2 on the cursor keys and SPACE (fire),
#             tools/joy2-keys.vicerc; the settings are not saved on exit
#             (+saveres), or the keys would stay a joystick in every session
# VICE_ARGS:  more options

VICE=${VICE:-x64sc}
VERA_BASE=${VERA_BASE:-0xDF00}
JOYKEYS_RC="$(dirname "$0")/joy2-keys.vicerc"

# a file without an option is autostarted by VICE (the first program of a disk)
exec "$VICE" -vera -verabase "$VERA_BASE" ${SDCARD:+-verasdcard "$SDCARD"} \
    ${JOYKEYS:+-addconfig "$JOYKEYS_RC" +saveres} $VICE_ARGS "$@"
