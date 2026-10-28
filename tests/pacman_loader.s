; pacman_loader.s - the loader of Pac-Man (pacman.d64): a progress bar on
; the C64 screen, in the font of the arcade, while "PACMAN DATA"
; (pacman.prg) is read from the disk, the VERA black meanwhile; then the
; program is RUN.
;
;   ca65 -t c64 -I ../include -D MAIN_BYTES=<size of pacman.prg> pacman_loader.s
;   ld65 -C pacman_loader.cfg
;
; The program loads from $0801 up to $AEFF, so the loader keeps out of its
; way in the VIC bank 3: the screen at $C000, the loader at $C400, the font
; at $C800.  The BASIC line "SYS 2061" starts a stub that copies the loader
; up there and builds the font (gen/pacfont.inc, pacman_font.py: the glyphs
; of the arcade at the C64 screen codes), then jumps to the loader.
;
; The file is read through the KERNAL (OPEN, CHRIN) as fast as LOAD reads
; it, every byte to its address; the bar and the percentage move with the
; bytes read against the thresholds computed here from MAIN_BYTES, so the
; C64 never divides.  At the end of the file the VIC bank and the font of the
; C64 are back, the BASIC pointers are set to the end of the program and
; BASIC runs it, as RUN would.

.include "vera_common.inc"
.macpack cbm

.ifndef MAIN_BYTES
.error "MAIN_BYTES: the size of pacman.prg in bytes (-D MAIN_BYTES=...)"
.endif

; KERNAL
SETLFS          = $FFBA
SETNAM          = $FFBD
OPEN            = $FFC0
CLOSE           = $FFC3
CHKIN           = $FFC6
CLRCHN          = $FFCC
CHRIN           = $FFCF
CHROUT          = $FFD2
STATUS          = $90                   ; ST: bit 6 end of file
FA              = $BA                   ; the device of the last load

; BASIC
VARTAB          = $2D                   ; end of the program, start of the variables
RUNC            = $A659                 ; TXTPTR to the start of the program, CLR
NEWSTT          = $A7AE                 ; run from TXTPTR

; VIC bank 3
SCREEN          = $C000
FONT            = $C800
VIC_MEM         = $D018                 ; screen and font of the bank
VIC_MEM_LOADER  = $02                   ; screen +$0000, font +$0800
CIA2_PRA        = $DD00                 ; bits 0-1: the VIC bank, inverted
COLOUR          = $D800
BORDER          = $D020
BACKGROUND      = $D021

LFN             = 2                     ; logical file and secondary address
BAR_ROW         = 12
BAR_COL         = 4
BAR_CELLS       = 32
PCT_ROW         = 14
PCT_COL         = 18

ptr             = $FB                   ; where the next byte goes
count           = $FD                   ; bytes read

YELLOW          = 7
WHITE           = 1
BLUE            = 6
PINK            = 10

.segment "LOADADDR"
        .addr   * + 2

.segment "EXEHDR"                       ; 10 SYS2061
        .word   basic_end
        .word   10
        .byte   $9E, "2061", 0
basic_end:
        .word   0

.segment "STUB"
        ldx     #0
@copy:  .repeat 4, page                 ; the loader, at most 1 KB
        lda     __LOADER_LOAD__ + page * 256, x
        sta     __LOADER_RUN__ + page * 256, x
        .endrep
        lda     #0                      ; the font blank
        .repeat 8, page
        sta     FONT + page * 256, x
        .endrep
        inx
        bne     @copy

        lda     #<glyphs                ; the glyphs of the arcade
        sta     ptr
        lda     #>glyphs
        sta     ptr + 1
        ldx     #FONT_GLYPHS
@glyph: ldy     #0                      ; count = FONT + code * 8 - 1: the rows
        lda     (ptr), y                ; are at y = 1-8, after the code
        sta     count
        sty     count + 1
        .repeat 3
        asl     count
        rol     count + 1
        .endrep
        lda     count
        clc
        adc     #<(FONT - 1)
        sta     count
        lda     count + 1
        adc     #>(FONT - 1)
        sta     count + 1
@row:   iny
        lda     (ptr), y
        sta     (count), y
        cpy     #8
        bne     @row
        lda     ptr
        clc
        adc     #9
        sta     ptr
        bcc     :+
        inc     ptr + 1
:       dex
        bne     @glyph
        jmp     start

glyphs:
.include "gen/pacfont.inc"

.import __LOADER_LOAD__, __LOADER_RUN__, __LOADER_SIZE__
.assert __LOADER_SIZE__ <= 1024, error, "the loader is more than 1 KB"

.segment "LOADER"

start:
        jsr     vera_black
        lda     CIA2_PRA
        sta     cia2_pra
        lda     VIC_MEM
        sta     vic_mem
        jsr     draw_screen
        lda     CIA2_PRA                ; the VIC bank 3
        and     #$FC
        sta     CIA2_PRA
        lda     #VIC_MEM_LOADER
        sta     VIC_MEM

        lda     FA                      ; the device it was loaded from
        cmp     #8
        bcs     :+
        lda     #8
:       tax
        lda     #LFN
        ldy     #LFN
        jsr     SETLFS
        lda     #name_end - name
        ldx     #<name
        ldy     #>name
        jsr     SETNAM
        jsr     OPEN
        bcs     error
        ldx     #LFN
        jsr     CHKIN
        bcs     error

        lda     #0
        sta     count
        sta     count + 1
        jsr     CHRIN                   ; the load address
        sta     ptr
        jsr     check
        jsr     CHRIN
        sta     ptr + 1
        jsr     check

@byte:  jsr     CHRIN
        ldy     #0
        sta     (ptr), y
        inc     ptr
        bne     :+
        inc     ptr + 1
:       jsr     check
        jmp     @byte

; a byte read: the progress, the status
check:
        inc     count
        bne     :+
        inc     count + 1
:       ldx     pct                     ; past the threshold of the next percent?
        cpx     #100
        bcs     @status
        lda     count + 1
        cmp     thr_hi, x
        bcc     @status
        bne     @next
        lda     count
        cmp     thr_lo, x
        bcc     @status
@next:  inc     pct
        jsr     draw_progress
@status:
        lda     STATUS
        bne     :+
        rts
:       pla                             ; out of the loop
        pla
        lda     STATUS
        and     #$BF                    ; an error, not the end of the file
        bne     error
        jsr     done
        lda     #100
        sta     pct
        jsr     draw_progress
        jsr     vic_back
        lda     ptr                     ; the end of the program, as LOAD leaves it
        sta     VARTAB
        lda     ptr + 1
        sta     VARTAB + 1
        jsr     RUNC
        jmp     NEWSTT

error:
        jsr     done
        jsr     vic_back
        ldx     #0
:       lda     msg_error, x
        jsr     CHROUT
        inx
        cpx     #msg_error_end - msg_error
        bne     :-
        rts                             ; back to BASIC

done:
        jsr     CLRCHN
        lda     #LFN
        jmp     CLOSE

; the VIC bank and the font of the C64 again
vic_back:
        lda     cia2_pra
        sta     CIA2_PRA
        lda     vic_mem
        sta     VIC_MEM
        rts

; the VERA selected, black: no layers and no sprites, black border and
; colour 0; the output mode as it is, VGA if off (as test_pacman.c)
vera_black:
        lda     #1
        sta     VERA_CARDSEL
        lda     #0
        sta     VERA_CTRL
        lda     VERA_DC_VIDEO
        and     #$0F
        bit     vga
        bne     :+
        ora     #$01
:       sta     VERA_DC_VIDEO
        lda     #0
        sta     VERA_DC_BORDER
        sta     VERA_ADDR_L             ; palette entry 0 ($1FA00)
        lda     #$FA
        sta     VERA_ADDR_M
        lda     #VERA_INC1 | 1
        sta     VERA_ADDR_H
        lda     #0
        sta     VERA_DATA0
        sta     VERA_DATA0
        rts

draw_screen:
        lda     #0
        sta     BORDER
        sta     BACKGROUND
        ldx     #0
:       lda     #' '
        sta     SCREEN, x
        sta     SCREEN + $100, x
        sta     SCREEN + $200, x
        sta     SCREEN + $2E8, x
        dex
        bne     :-
        ldx     #msg_title_end - msg_title - 1
:       lda     msg_title, x
        sta     SCREEN + 8 * 40 + 16, x
        lda     #YELLOW
        sta     COLOUR + 8 * 40 + 16, x
        dex
        bpl     :-
        ldx     #msg_load_end - msg_load - 1
:       lda     msg_load, x
        sta     SCREEN + 10 * 40 + 15, x
        lda     #WHITE
        sta     COLOUR + 10 * 40 + 15, x
        dex
        bpl     :-
        ldx     #msg_namco_end - msg_namco - 1
:       lda     msg_namco, x
        sta     SCREEN + 20 * 40 + 13, x
        lda     #PINK
        sta     COLOUR + 20 * 40 + 13, x
        dex
        bpl     :-
        ldx     #BAR_CELLS - 1          ; the empty bar
:       lda     #$A0
        sta     SCREEN + BAR_ROW * 40 + BAR_COL, x
        lda     #BLUE
        sta     COLOUR + BAR_ROW * 40 + BAR_COL, x
        dex
        bpl     :-
        ldx     #3
        lda     #WHITE
:       sta     COLOUR + PCT_ROW * 40 + PCT_COL, x
        dex
        bpl     :-
        lda     #0
        sta     pct
        ; fall through: 0%

; the bar up to the cell of pct, and pct as "nnn%"
draw_progress:
        ldy     pct
        ldx     cells, y
        beq     @pct
        dex
        lda     #YELLOW
:       sta     COLOUR + BAR_ROW * 40 + BAR_COL, x
        dex
        bpl     :-
@pct:   lda     pct
        ldx     #' '                    ; hundreds
        cmp     #100
        bcc     :+
        sbc     #100
        ldx     #'1'
:       stx     SCREEN + PCT_ROW * 40 + PCT_COL
        ldx     #'0' - 1                ; tens
        sec
:       inx
        sbc     #10
        bcs     :-
        adc     #10 + '0'
        cpx     #'0'
        bne     :+
        ldy     SCREEN + PCT_ROW * 40 + PCT_COL
        cpy     #' '
        bne     :+
        ldx     #' '                    ; no leading zero
:       stx     SCREEN + PCT_ROW * 40 + PCT_COL + 1
        sta     SCREEN + PCT_ROW * 40 + PCT_COL + 2
        lda     #'%'
        sta     SCREEN + PCT_ROW * 40 + PCT_COL + 3
        rts

vga:    .byte   $03                     ; the output mode bits

name:   .byte   "pacman data,p,r"       ; PETSCII: upper case on the C64
name_end:

msg_title:      scrcode "pac-man"
msg_title_end:
msg_load:       scrcode "loading..."
msg_load_end:
msg_namco:      scrcode "1980 namco ltd."
msg_namco_end:
msg_error:      .byte   13, "load error", 13
msg_error_end:

; the bytes read for 1% ... 100%, and the cells of the bar for 0% ... 100%
thr_lo:
.repeat 100, i
        .byte   <(MAIN_BYTES * (i + 1) / 100)
.endrep
thr_hi:
.repeat 100, i
        .byte   >(MAIN_BYTES * (i + 1) / 100)
.endrep
cells:
.repeat 101, i
        .byte   i * BAR_CELLS / 100
.endrep

pct:            .res    1
cia2_pra:       .res    1
vic_mem:        .res    1
