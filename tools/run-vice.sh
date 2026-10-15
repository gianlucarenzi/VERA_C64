#!/bin/sh
# run-vice.sh - start VICE with the VERA card and a disk or a program.
#
#   tools/run-vice.sh vera-c64.d64          the disk in drive 8
#   tools/run-vice.sh tests/testfx.prg      autostart a program
#
# VICE:       VICE=x64sc-3.10 (default x64sc; it needs the VERA card, see
#             https://github.com/gianlucarenzi/vice/tree/vera-io1)
# VERA_BASE:  0xDE00 (IO1, default) or 0xDF00 (IO2)
# SDCARD:     raw SD card image on the SPI port of the VERA
# VICE_ARGS:  more options

VICE=${VICE:-x64sc}
VERA_BASE=${VERA_BASE:-0xDE00}

# a file without an option is autostarted by VICE (the first program of a disk)
exec "$VICE" -vera -verabase "$VERA_BASE" ${SDCARD:+-verasdcard "$SDCARD"} $VICE_ARGS "$@"
