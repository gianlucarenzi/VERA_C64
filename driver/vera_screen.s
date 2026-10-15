; vera_screen.s — VERA screen driver for the C64 (the VERA.SYS of the Atari
; PBI card, rewritten around the KERNAL vectors).
;
; LOAD"VERA80",8 : RUN   (or VERA60 / VERA40) installs a resident driver at
; $C000-$CFFF that takes the screen of the KERNAL over:
;
;   IBSOUT ($0326)  CHROUT to device 3 (the screen) is drawn on the VERA:
;                   PETSCII with the C64 control codes, quote and insert
;                   mode, reverse, colours, upper/lower case, logical lines;
;   IBASIN ($0324)  CHRIN from device 0 (the keyboard) is a screen editor on
;                   the VERA: the typed line is echoed, RETURN hands over the
;                   logical line under the cursor (BASIC direct mode, INPUT);
;   CINV   ($0314)  cursor blink, upper/lower case (C= + SHIFT) and border
;                   colour follow the KERNAL, then the old IRQ handler runs;
;   IERROR ($0300)  BASIC's warm start (RUN/STOP + RESTORE) hooks the
;                   vectors again after the KERNAL restored them.
;
; After a reset SYS 49152 installs the driver again (it is still in RAM and
; the fonts are still in VRAM: the VERA has no reset input).
;
; The VIC-II screen is left alone: it keeps what it showed at install time.
;
; Build: ca65 -D VERA_BASE=$DE00 [-D FONT_8X8=1] [-D MODE_40X30=1 -D FONT_8X8=1]

        .setcpu "6502"

        .include "vera_common.inc"

        .import __RESIDENT_LOAD__, __RESIDENT_RUN__, __RESIDENT_SIZE__

; ============================================================================
; Driver layout and C64 addresses
; ============================================================================

COLS                    = SCREEN_COLS               ; viewport width  in characters
ROWS                    = SCREEN_ROWS               ; viewport height in characters
LINE_CELLS              = 80                        ; logical line: 80 characters, as on the C64
LINE_ROWS               = LINE_CELLS / COLS         ; physical rows of a logical line (1 or 2)
INBUF_MAX               = 88                        ; BASIC input buffer length

FONT_ADDR               = $019000                   ; set 0 (upper/graphics), 256 chars, 2 KB aligned
FONT_SET_SIZE           = 256 * TILE_HEIGHT         ; bytes of one set (incl. reversed half)
FONT1_ADDR              = FONT_ADDR + FONT_SET_SIZE ; set 1 (lower/upper)
.if TILE_HEIGHT = 16
TILE_H_BIT              = $02                       ; L1_TILEBASE bit 1: 16 pixel tall tiles
.else
TILE_H_BIT              = $00
.endif
TILEBASE0               = ((FONT_ADDR >> 9) & $FC) | TILE_H_BIT
TILEBASE1               = ((FONT1_ADDR >> 9) & $FC) | TILE_H_BIT
MAP_H                   = VERA_INC1 | ^SCREEN_ADDR  ; ADDR_H of the tilemap, increment 1
MAP_H4                  = VERA_INC4 | ^SCREEN_ADDR  ; ADDR_H of the tilemap, increment 4

DC_HSTOP_640            = $A0                       ; DC_HSTOP: 640 / 4
DC_VSTOP_480            = $F0                       ; DC_VSTOP: 480 / 2

BLINK_JIFFIES           = 20                        ; cursor phase, as the KERNAL

; KERNAL / BASIC
IERROR                  = $0300
CINV                    = $0314
IBASIN                  = $0324
IBSOUT                  = $0326
DFLTN                   = $99                       ; input device
DFLTO                   = $9A                       ; output device
NDX                     = $C6                       ; keys in the keyboard buffer
BLNSW                   = $CC                       ; KERNAL cursor blink: 0 = on
PNTR                    = $D3                       ; cursor column (PLOT)
TBLX                    = $D6                       ; cursor row    (PLOT)
COLOR                   = $0286                     ; current text colour
MODE                    = $0291                     ; $80: C= + SHIFT locked
LP2                     = $E5B4                     ; KERNAL: take a key from the buffer (call with I=1)
VIC_BORDER              = $D020
VIC_BG                  = $D021
VIC_MEMSETUP            = $D018                     ; bit 1: lower case character set
CHROUT                  = $FFD2

ZP_PTR                  = $FB                       ; installer only (saved and restored)

; ============================================================================
; BASIC stub and installer (loaded at $0801, used once)
; ============================================================================

        .segment "EXEHDR"
        .word   @next, 10
        .byte   $9E, "2061", 0                      ; 10 SYS 2061
@next:  .word   0

        .segment "STARTUP"
install:
        jsr     detect
        bcc     @found
        ldx     #0
@msg:   lda     not_found,x
        beq     @out
        jsr     CHROUT
        inx
        bne     @msg
@out:   rts

@found: jsr     unhook                              ; a driver already there: off
        lda     ZP_PTR                              ; resident part to $C000
        pha
        lda     ZP_PTR+1
        pha
        lda     #<__RESIDENT_LOAD__
        sta     ZP_PTR
        lda     #>__RESIDENT_LOAD__
        sta     ZP_PTR+1
        lda     #>__RESIDENT_RUN__                  ; (RUN again: the copy restarts)
        sta     @dst+2
        ldy     #0
        ldx     #>(__RESIDENT_SIZE__ + 255)
@copy:  lda     (ZP_PTR),y
@dst:   sta     __RESIDENT_RUN__,y
        iny
        bne     @copy
        inc     ZP_PTR+1
        inc     @dst+2
        dex
        bne     @copy

        lda     #0                                  ; fonts to VRAM
        sta     VERA_CTRL
        sta     VERA_ADDR_L
        lda     #>FONT_ADDR
        sta     VERA_ADDR_M
        lda     #(VERA_INC1 | ^FONT_ADDR)
        sta     VERA_ADDR_H
        lda     #<font
        sta     ZP_PTR
        lda     #>font
        sta     ZP_PTR+1
        ldy     #0
        ldx     #>(2 * FONT_SET_SIZE)
@font:  lda     (ZP_PTR),y
        sta     VERA_DATA0
        iny
        bne     @font
        inc     ZP_PTR+1
        dex
        bne     @font

        pla
        sta     ZP_PTR+1
        pla
        sta     ZP_PTR
        jmp     reinstall

; unhook — a driver of this kind already in RAM: its vectors back to the
; KERNAL (they are saved at $C007), it is overwritten next
.macro  UNHOOK vec, idx
        .local  skip
        lda     vec+1
        and     #$F0
        cmp     #>__RESIDENT_RUN__
        bne     skip
        lda     __RESIDENT_RUN__+7+idx*2
        sta     vec
        lda     __RESIDENT_RUN__+8+idx*2
        sta     vec+1
skip:
.endmacro

unhook:
        ldx     #3
@sig:   lda     __RESIDENT_RUN__+3,x
        cmp     signature_text,x
        bne     unhook_done
        dex
        bpl     @sig
        sei
        UNHOOK  IBSOUT, 0
        UNHOOK  IBASIN, 1
        UNHOOK  CINV, 2
        UNHOOK  IERROR, 3
        cli
unhook_done:
        rts

signature_text:
        .byte   "VERA"

; detect — C clear when the VERA answers: DCSEL 63 reads 'V' and the major
; version of a VERA with FX
detect:
        lda     #VERA_DCSEL63
        sta     VERA_CTRL
        lda     VERA_BASE+$09
        ldx     VERA_BASE+$0A
        ldy     #0
        sty     VERA_CTRL
        cmp     #VERA_ID_V
        bne     @no
        cpx     #VERA_ID_MAJOR_MIN
        bcc     @no
        clc
        rts
@no:    sec
        rts

not_found:
        .byte   13, "VERA NOT FOUND AT $"
        .byte   .sprintf("%04X", VERA_BASE), 13, 0

        .segment "FONT"
font:
.if TILE_HEIGHT = 16
        .incbin "gen/petscii16.bin"
.else
        .incbin "gen/petscii8.bin"
.endif

; ============================================================================
; Resident driver ($C000)
; ============================================================================

        .segment "RESIDENT"

; SYS 49152: hook the vectors again, set the VERA up, clear the screen
reinstall:
        jmp     do_reinstall
signature:
        .byte   "VERA"                              ; signature at $C003
old_vectors:                                        ; $C007: the KERNAL vectors before the driver
old_bsout:      .res 2
old_basin:      .res 2
old_cinv:       .res 2
old_ierror:     .res 2

do_reinstall:
        sei
        jsr     hook_vectors
        cli
        jsr     init_hw
        jsr     reset_state
        jsr     clear_screen
        ldx     #0
@ban:   lda     banner,x
        beq     @done
        stx     tmp4
        jsr     put_petscii
        ldx     tmp4
        inx
        bne     @ban
@done:  jmp     mirror_plot

banner:
        .byte   "VERA "
        .byte   .sprintf("%dX%d", COLS, ROWS)
        .byte   " SCREEN AT $"
        .byte   .sprintf("%04X", VERA_BASE), 13, 0

; hook_vectors — point CHROUT, CHRIN, IRQ and IERROR to the driver; a vector
; that already does is left as it is (the old one stays saved)
hook_vectors:
        lda     IBSOUT+1
        cmp     #>chrout_hook
        beq     @in
        lda     IBSOUT
        sta     old_bsout
        lda     IBSOUT+1
        sta     old_bsout+1
        lda     #<chrout_hook
        sta     IBSOUT
        lda     #>chrout_hook
        sta     IBSOUT+1
@in:    lda     IBASIN+1
        cmp     #>chrin_hook
        beq     @irq
        lda     IBASIN
        sta     old_basin
        lda     IBASIN+1
        sta     old_basin+1
        lda     #<chrin_hook
        sta     IBASIN
        lda     #>chrin_hook
        sta     IBASIN+1
@irq:   lda     CINV+1
        cmp     #>irq_hook
        beq     @err
        lda     CINV
        sta     old_cinv
        lda     CINV+1
        sta     old_cinv+1
        lda     #<irq_hook
        sta     CINV
        lda     #>irq_hook
        sta     CINV+1
@err:   lda     IERROR+1
        cmp     #>ierror_hook
        beq     @done
        lda     IERROR
        sta     old_ierror
        lda     IERROR+1
        sta     old_ierror+1
        lda     #<ierror_hook
        sta     IERROR
        lda     #>ierror_hook
        sta     IERROR+1
@done:  rts

; init_hw — layer 1: 128x64 text map, 1 bpp, the PETSCII fonts; VGA output
init_hw:
        lda     #VERA_DCSEL2                        ; FX off
        sta     VERA_CTRL
        lda     #0
        sta     VERA_FX_CTRL
        sta     VERA_FX_MULT
        lda     #VERA_DCSEL1
        sta     VERA_CTRL
        lda     #0
        sta     VERA_DC_HSTART
        sta     VERA_DC_VSTART
        lda     #DC_HSTOP_640
        sta     VERA_DC_HSTOP
        lda     #DC_VSTOP_480
        sta     VERA_DC_VSTOP
        lda     #VERA_DCSEL0
        sta     VERA_CTRL
        lda     #VERA_MAP_128x64                    ; 1 bpp, 16 colour text
        sta     VERA_L1_CONFIG
        lda     #SCREEN_MAPBASE
        sta     VERA_L1_MAPBASE
        lda     #0
        sta     VERA_L1_HSCR_L
        sta     VERA_L1_HSCR_H
        sta     VERA_L1_VSCR_L
        sta     VERA_L1_VSCR_H
        lda     #(VERA_VIDEO_VGA | VERA_LAYER1_EN)
        sta     VERA_DC_VIDEO
        lda     #HSCALE_VAL
        sta     VERA_DC_HSCALE
        lda     #VSCALE_VAL
        sta     VERA_DC_VSCALE
        lda     VIC_BORDER
        and     #$0F
        sta     last_border
        sta     VERA_DC_BORDER
        lda     VIC_MEMSETUP
        and     #$02
        sta     last_case
        jmp     set_case

; set_case — the font of last_case (0: upper/graphics, 2: lower/upper)
set_case:
        lda     last_case
        beq     @upper
        lda     #TILEBASE1
        .byte   $2C                                 ; bit abs: skip the next lda
@upper: lda     #TILEBASE0
        sta     VERA_L1_TILEBASE
        rts

; reset_state — no quote, insert or reverse mode, no input line pending
reset_state:
        lda     #0
        sta     quote
        sta     insert_cnt
        sta     rvs
        sta     in_pending
        sta     blink_enabled
        sta     blink_on
        lda     #1
        sta     BLNSW                               ; the KERNAL does not blink on the VIC
        rts

; ============================================================================
; IERROR: BASIC warm start and error messages
; ============================================================================

; RUN/STOP + RESTORE restores the KERNAL vectors and starts BASIC again
; through IERROR: hook again and clear the screen, as the C64 does
ierror_hook:
        pha
        lda     IBSOUT+1
        cmp     #>chrout_hook
        beq     @ours
        txa
        pha
        tya
        pha
        jsr     do_reinstall
        pla
        tay
        pla
        tax
@ours:  pla
        jmp     (old_ierror)

; ============================================================================
; IRQ: cursor blink, case and border follow the VIC
; ============================================================================

irq_hook:
        lda     VIC_MEMSETUP                        ; C= + SHIFT switched the case
        and     #$02
        cmp     last_case
        beq     @border
        sta     last_case
        jsr     set_case                            ; L1_TILEBASE: not DCSEL banked
@border:
        lda     VIC_BORDER
        and     #$0F
        cmp     last_border
        beq     @blink
        sta     last_border
        tax
        lda     VERA_CTRL                           ; DCSEL 0 for DC_BORDER, then back
        pha
        and     #$01
        sta     VERA_CTRL
        stx     VERA_DC_BORDER
        pla
        sta     VERA_CTRL
@blink: lda     blink_enabled                       ; only while CHRIN waits for a key:
        beq     @chain                              ; the main program is not on the VERA
        dec     blink_cnt
        bne     @chain
        lda     #BLINK_JIFFIES
        sta     blink_cnt
        lda     VERA_CTRL
        pha
        lda     #0
        sta     VERA_CTRL
        jsr     addr_cursor
        lda     blink_on
        eor     #1
        sta     blink_on
        beq     @off
        lda     blink_char                          ; reversed, in the current colour
        eor     #$80
        sta     VERA_DATA0
        jsr     cell_color
        sta     VERA_DATA0
        jmp     @rest
@off:   lda     blink_char
        sta     VERA_DATA0
        lda     blink_color
        sta     VERA_DATA0
@rest:  pla
        sta     VERA_CTRL
@chain: jmp     (old_cinv)

; cursor_show — remember the cell under the cursor and let the IRQ blink it
cursor_show:
        lda     #0
        sta     VERA_CTRL
        jsr     addr_cursor
        lda     VERA_DATA0
        sta     blink_char
        lda     VERA_DATA0
        sta     blink_color
        lda     #0
        sta     blink_on
        lda     #1
        sta     blink_cnt                           ; first phase at the next IRQ
        sta     blink_enabled
        rts

; cursor_hide — stop the blink, put the cell back
cursor_hide:
        sei
        lda     #0
        sta     blink_enabled
        lda     blink_on
        beq     @done
        lda     #0
        sta     blink_on
        sta     VERA_CTRL
        jsr     addr_cursor
        lda     blink_char
        sta     VERA_DATA0
        lda     blink_color
        sta     VERA_DATA0
@done:  cli
        rts

; ============================================================================
; CHROUT
; ============================================================================

chrout_hook:
        pha
        lda     DFLTO
        cmp     #3
        beq     @screen
        pla
        jmp     (old_bsout)
@screen:
        pla
        sta     save_a
        stx     save_x
        sty     save_y
        jsr     sync_plot
        lda     save_a
        jsr     put_petscii
        jsr     mirror_plot
        ldx     save_x
        ldy     save_y
        lda     save_a
        clc
        rts

; sync_plot — PLOT (or a POKE) moved the cursor: take its position
sync_plot:
        lda     PNTR
        cmp     plot_col
        bne     @moved
        lda     TBLX
        cmp     plot_row
        beq     @done
@moved: lda     PNTR
        cmp     #COLS
        bcc     :+
        lda     #COLS-1
:       sta     cur_col
        lda     TBLX
        cmp     #ROWS
        bcc     :+
        lda     #ROWS-1
:       sta     cur_row
@done:  rts

; mirror_plot — the cursor for PLOT, POS() and TAB()
mirror_plot:
        lda     cur_col
        sta     PNTR
        sta     plot_col
        lda     cur_row
        sta     TBLX
        sta     plot_row
        rts

; put_petscii — draw or execute one PETSCII character (A); X and Y are lost
put_petscii:
        cmp     #$0D
        beq     @cr
        cmp     #$8D
        bne     @not_cr
@cr:    jmp     do_cr
@not_cr:
        ldx     insert_cnt                          ; insert mode: everything literal
        beq     @not_ins
        dec     insert_cnt
        jmp     put_literal
@not_ins:
        ldx     quote
        beq     @control
        cmp     #$14                                ; quote mode: DEL still deletes
        beq     @control
        cmp     #'"'
        beq     @control
        jmp     put_literal
@control:
        tax
        and     #$60                                ; $00-$1F and $80-$9F: control codes
        beq     @code
        txa
        jmp     put_char
@code:  txa
        sta     code
        ldx     #(ctrl_codes_end - ctrl_codes - 1)
@find:  cmp     ctrl_codes,x
        beq     @found
        dex
        bpl     @find
        rts                                         ; not a code of the screen
@found: lda     ctrl_hi,x
        pha
        lda     ctrl_lo,x
        pha
        rts

ctrl_codes:
        .byte   $93, $13, $11, $91, $1D, $9D, $14, $94, $12, $92, $0E, $8E, $08, $09
        .byte   $90, $05, $1C, $9F, $9C, $1E, $1F, $9E, $81, $95, $96, $97, $98, $99, $9A, $9B
ctrl_codes_end:
ctrl_lo:
        .lobytes do_clear-1, do_home-1, do_down-1, do_up-1, do_right-1, do_left-1
        .lobytes do_delete-1, do_insert-1, do_rvs_on-1, do_rvs_off-1, do_lower-1
        .lobytes do_upper-1, do_lock-1, do_unlock-1
        .repeat 16
        .lobytes do_colour-1
        .endrepeat
ctrl_hi:
        .hibytes do_clear-1, do_home-1, do_down-1, do_up-1, do_right-1, do_left-1
        .hibytes do_delete-1, do_insert-1, do_rvs_on-1, do_rvs_off-1, do_lower-1
        .hibytes do_upper-1, do_lock-1, do_unlock-1
        .repeat 16
        .hibytes do_colour-1
        .endrepeat

; colour codes in the order of the C64 colours 0-15
colour_codes:
        .byte   $90, $05, $1C, $9F, $9C, $1E, $1F, $9E, $81, $95, $96, $97, $98, $99, $9A, $9B

do_colour:
        lda     code
        ldx     #15
@find:  cmp     colour_codes,x
        beq     @set
        dex
        bpl     @find
        rts
@set:   stx     COLOR
        rts

do_rvs_on:
        lda     #$80
        .byte   $2C
do_rvs_off:
        lda     #0
        sta     rvs
        rts

do_lower:
        lda     VIC_MEMSETUP                        ; as the KERNAL: the VIC too
        ora     #$02
        bne     set_vic_case
do_upper:
        lda     VIC_MEMSETUP
        and     #$FD
set_vic_case:
        bit     MODE                                ; locked by CHR$(8)
        bmi     @done
        sta     VIC_MEMSETUP
        and     #$02
        sta     last_case
        jmp     set_case
@done:  rts

do_lock:
        lda     #$80
        .byte   $2C
do_unlock:
        lda     #0
        sta     MODE
        rts

do_home:
        lda     #0
        sta     cur_col
        sta     cur_row
        rts

do_up:
        lda     cur_row
        beq     @done
        dec     cur_row
@done:  rts

do_down:
        inc     cur_row
        lda     cur_row
        cmp     #ROWS
        bcc     @done
        dec     cur_row
        jsr     scroll_up
        ldx     #ROWS-1
        lda     #0
        sta     link,x
@done:  rts

do_left:
        lda     cur_col
        beq     @prev
        dec     cur_col
        rts
@prev:  lda     cur_row
        beq     @done
        dec     cur_row
        lda     #COLS-1
        sta     cur_col
@done:  rts

do_right:
        inc     cur_col
        lda     cur_col
        cmp     #COLS
        bcc     @done
        lda     #0
        sta     cur_col
        jmp     do_down
@done:  rts

; do_cr — RETURN: to the start of the row after the logical line; quote,
; insert and reverse mode end
do_cr:
        lda     #0
        sta     quote
        sta     insert_cnt
        sta     rvs
        sta     cur_col
@skip:  ldx     cur_row                             ; skip the rest of the logical line
        inx
        cpx     #ROWS
        bcs     @next
        lda     link,x
        beq     @next
        stx     cur_row
        bne     @skip
@next:  jsr     do_down
        ldx     cur_row
        lda     #0
        sta     link,x
        rts

; put_literal — a character of quote or insert mode: control codes are shown
; reversed ($00-$1F as $80-$9F, $80-$9F as $C0-$DF)
put_literal:
        tax
        and     #$60
        bne     put_char_x
        txa
        bmi     @hi
        ora     #$80                                ; $00-$1F
        bne     put_code
@hi:    sec
        sbc     #$40                                ; $80-$9F -> $40-$5F
        ora     #$80
        bne     put_code
put_char_x:
        txa

; put_char — a printable PETSCII character: screen code, reverse, advance
put_char:
        cmp     #'"'
        bne     @conv
        pha
        lda     quote
        eor     #1
        sta     quote
        pla
@conv:  cmp     #$40
        bcc     @rev                                ; $20-$3F: same
        cmp     #$60
        bcc     @sub40                              ; $40-$5F -> $00-$1F
        cmp     #$80
        bcc     @sub20                              ; $60-$7F -> $40-$5F
        cmp     #$C0
        bcc     @sub40                              ; $A0-$BF -> $60-$7F
        cmp     #$FF
        bne     @sub80                              ; $C0-$FE -> $40-$7E
        lda     #$5E                                ; pi
        bne     @rev
@sub80: and     #$7F
        bpl     @rev
@sub20: sec
        sbc     #$20
        bne     @rev
@sub40: and     #$3F
        cmp     #$20                                ; $A0-$BF: $60-$7F
        bcc     @rev
        ora     #$40
@rev:   ora     rvs
put_code:
        pha
        lda     #0
        sta     VERA_CTRL
        jsr     addr_cursor
        pla
        sta     VERA_DATA0
        jsr     cell_color
        sta     VERA_DATA0
        ; advance, wrap into a new row of the logical line or a new line
        inc     cur_col
        lda     cur_col
        cmp     #COLS
        bcc     @done
        lda     #0
        sta     cur_col
        ldx     cur_row                             ; the next row continues the line
        inx
        cpx     #ROWS
        bcs     @notcont
        lda     link,x
        beq     @notcont
        stx     cur_row
        rts
@notcont:
.if LINE_ROWS > 1
        jsr     line_start                          ; X = first row, Y = rows
        cpy     #LINE_ROWS
        bcs     @newline
        ldx     cur_row                             ; the line gets one more row:
        inx                                         ; open it below (C64 style)
        cpx     #ROWS
        bcc     @insert
        jsr     scroll_up
        ldx     #ROWS-1
        lda     #1
        sta     link,x
        rts
@insert:
        stx     cur_row
        txa
        jsr     insert_row
        ldx     cur_row
        lda     #1
        sta     link,x
        rts
@newline:
.endif
        jsr     do_down
        ldx     cur_row
        lda     #0
        sta     link,x
@done:  rts

; cell_color — A = colour byte of a new cell: background << 4 | COLOR
cell_color:
        lda     COLOR
        and     #$0F
        ora     bg_nibble
        rts

; addr_cursor — data port 0 (ADDRSEL 0 already selected) on the cursor cell
addr_cursor:
        lda     cur_col
        asl     a
        sta     VERA_ADDR_L
        lda     cur_row
        clc
        adc     #SCREEN_ADDR_M
        sta     VERA_ADDR_M
        lda     #MAP_H
        sta     VERA_ADDR_H
        rts

; ============================================================================
; Logical lines, DEL and INST
; ============================================================================

; line_start — X = first row of the logical line of the cursor, Y = its rows
line_start:
        ldx     cur_row
@back:  lda     link,x
        beq     @count
        dex
        bne     @back
@count: stx     tmp
        ldy     #1
@more:  inx
        cpx     #ROWS
        bcs     @done
        lda     link,x
        beq     @done
        iny
        bne     @more
@done:  ldx     tmp
        rts

; line_read — the cells of the logical line of the cursor into linebuf
; (char, colour); line_row = first row, line_len = cells, line_pos = cursor
line_read:
        jsr     line_start
        stx     line_row
        sty     line_nrows
        lda     #0
        sta     line_len
        tay
@row:   lda     #0
        sta     VERA_CTRL
        sta     VERA_ADDR_L
        txa
        clc
        adc     #SCREEN_ADDR_M
        sta     VERA_ADDR_M
        lda     #MAP_H
        sta     VERA_ADDR_H
        lda     #COLS
        sta     tmp
@cell:  lda     VERA_DATA0
        sta     linebuf,y
        iny
        lda     VERA_DATA0
        sta     linebuf,y
        iny
        inc     line_len
        dec     tmp
        bne     @cell
        inx
        dec     line_nrows
        bne     @row
        ; position of the cursor in the line
        lda     cur_row
        sec
        sbc     line_row
        tax
        lda     cur_col
@pos:   dex
        bmi     @set
        clc
        adc     #COLS
        bne     @pos
@set:   sta     line_pos
        rts

; line_write — linebuf back to the rows of the logical line
line_write:
        ldx     line_row
        ldy     #0
@row:   lda     #0
        sta     VERA_CTRL
        sta     VERA_ADDR_L
        txa
        clc
        adc     #SCREEN_ADDR_M
        sta     VERA_ADDR_M
        lda     #MAP_H
        sta     VERA_ADDR_H
        lda     #COLS
        sta     tmp
@cell:  lda     linebuf,y
        sta     VERA_DATA0
        iny
        lda     linebuf,y
        sta     VERA_DATA0
        iny
        dec     tmp
        bne     @cell
        inx
        cpy     line_bytes
        bcc     @row
        rts

; pos_to_cursor — cursor on cell line_pos of the logical line
pos_to_cursor:
        lda     line_pos
        ldx     line_row
@row:   cmp     #COLS
        bcc     @set
        sbc     #COLS
        inx
        bne     @row
@set:   sta     cur_col
        stx     cur_row
        rts

; do_delete — DEL: the character left of the cursor goes, the rest of the
; logical line moves left
do_delete:
        jsr     line_read
        lda     line_pos
        beq     @done                               ; start of the logical line
        dec     line_pos
        lda     line_len
        asl     a
        sta     line_bytes
        lda     line_pos
        asl     a
        tay
@move:  lda     linebuf+2,y
        sta     linebuf,y
        iny
        cpy     line_bytes
        bcc     @move
        lda     #' '
        sta     linebuf-2,y
        jsr     cell_color
        sta     linebuf-1,y
        jsr     line_write
        jmp     pos_to_cursor
@done:  rts

; do_insert — INST: a space at the cursor, the rest of the logical line moves
; right (when its last character is a space), insert mode for one character
do_insert:
        jsr     line_read
        lda     line_len
        asl     a
        sta     line_bytes
        tay
        lda     linebuf-2,y
        cmp     #' '
        bne     @done                               ; the line is full
        lda     line_pos
        asl     a
        sta     tmp
@move:  dey
        dey
        cpy     tmp
        beq     @put
        bcc     @put
        lda     linebuf-2,y
        sta     linebuf,y
        lda     linebuf-1,y
        sta     linebuf+1,y
        jmp     @move
@put:   ldy     tmp
        lda     #' '
        sta     linebuf,y
        jsr     cell_color
        sta     linebuf+1,y
        jsr     line_write
        inc     insert_cnt
@done:  rts

; ============================================================================
; Scrolling, clearing (FX 32-bit cache, as the Atari driver)
; ============================================================================

; copy_row — row A to row X, 4 bytes per write through the FX cache
copy_row:
        sta     tmp
        stx     tmp2
        lda     #VERA_DCSEL2
        sta     VERA_CTRL
        lda     #0
        sta     VERA_FX_MULT                        ; cache byte index 0
        lda     #(FX_CACHE_FILL_EN | FX_CACHE_WR_EN)
        sta     VERA_FX_CTRL
        lda     #VERA_DCSEL0                        ; source: port 0, increment 1
        sta     VERA_CTRL
        lda     #0
        sta     VERA_ADDR_L
        lda     tmp
        clc
        adc     #SCREEN_ADDR_M
        sta     VERA_ADDR_M
        lda     #MAP_H
        sta     VERA_ADDR_H
        lda     #(VERA_DCSEL0 | 1)                  ; destination: port 1, increment 4
        sta     VERA_CTRL
        lda     #0
        sta     VERA_ADDR_L
        lda     tmp2
        clc
        adc     #SCREEN_ADDR_M
        sta     VERA_ADDR_M
        lda     #MAP_H4
        sta     VERA_ADDR_H
        ldy     #(COLS * 2 / 4)
@dword: lda     VERA_DATA0                          ; 4 reads fill the cache
        lda     VERA_DATA0
        lda     VERA_DATA0
        lda     VERA_DATA0
        lda     #0
        sta     VERA_DATA1                          ; 1 write stores 32 bits
        dey
        bne     @dword
fx_off:
        lda     #VERA_DCSEL2
        sta     VERA_CTRL
        lda     #0
        sta     VERA_FX_CTRL
        sta     VERA_CTRL                           ; DCSEL 0, ADDRSEL 0
        rts

; clear_row — row A blank, in the current colours
clear_row:
        pha
        lda     #VERA_DCSEL6                        ; cache: ' ', colour, ' ', colour
        sta     VERA_CTRL
        jsr     cell_color
        tax
        lda     #' '
        sta     VERA_FX_CACHE_L
        stx     VERA_FX_CACHE_M
        sta     VERA_FX_CACHE_H
        stx     VERA_FX_CACHE_U
        lda     #VERA_DCSEL2
        sta     VERA_CTRL
        lda     #FX_CACHE_WR_EN
        sta     VERA_FX_CTRL
        lda     #VERA_DCSEL0
        sta     VERA_CTRL
        lda     #0
        sta     VERA_ADDR_L
        pla
        clc
        adc     #SCREEN_ADDR_M
        sta     VERA_ADDR_M
        lda     #MAP_H4
        sta     VERA_ADDR_H
        ldy     #(COLS * 2 / 4)
        lda     #0
@dword: sta     VERA_DATA0
        dey
        bne     @dword
        beq     fx_off

; scroll_up — rows 1..ROWS-1 to 0..ROWS-2, last row blank, links follow
scroll_up:
        ldx     #0
@row:   stx     tmp3
        inx
        txa
        ldx     tmp3
        jsr     copy_row
        ldx     tmp3
        lda     link+1,x
        sta     link,x
        inx
        cpx     #ROWS-1
        bcc     @row
        lda     #0
        sta     link,x
        lda     #ROWS-1
        jsr     clear_row
        lda     in_row                              ; the start of the input moved up
        bmi     @done
        dec     in_row
@done:  rts

; insert_row — rows A..ROWS-2 one down, row A blank, links follow
insert_row:
        sta     tmp3
        ldx     #ROWS-1
@row:   cpx     tmp3
        beq     @clear
        stx     tmp4
        dex
        txa
        ldx     tmp4
        jsr     copy_row
        ldx     tmp4
        lda     link-1,x
        sta     link,x
        dex
        bne     @row
@clear: lda     #0
        sta     link,x
        txa
        jmp     clear_row

; clear_screen — CLR: background from the VIC, all rows blank, cursor home
do_clear:
clear_screen:
        lda     VIC_BG
        and     #$0F
        asl     a
        asl     a
        asl     a
        asl     a
        sta     bg_nibble
        ldx     #ROWS-1
@row:   lda     #0
        sta     link,x
        stx     tmp3
        txa
        jsr     clear_row
        ldx     tmp3
        dex
        bpl     @row
        lda     #$FF
        sta     in_row
        jmp     do_home

; ============================================================================
; CHRIN: the screen editor
; ============================================================================

chrin_hook:
        lda     DFLTN
        beq     @keyboard
        jmp     (old_basin)
@keyboard:
        stx     save_x
        sty     save_y
        lda     in_pending
        bne     @give
        jsr     sync_plot
        lda     cur_row                             ; where the input starts (after
        sta     in_row                              ; a prompt of INPUT)
        lda     cur_col
        sta     in_col
@wait:  jsr     cursor_show
@key:   lda     NDX
        beq     @key
        jsr     cursor_hide
        sei
        jsr     LP2                                 ; A = key, I = 0
        cmp     #$0D
        beq     @return
        jsr     put_petscii                         ; echo, editing keys included
        jsr     mirror_plot
        jmp     @wait

@return:
        jsr     take_line                           ; the cursor stays: BASIC prints
        lda     #1                                  ; the RETURN
        sta     in_pending
        lda     #0
        sta     in_idx
@give:  ldx     in_idx
        cpx     in_len
        bcs     @eol
        lda     inbuf,x
        inc     in_idx
        bne     @ret
@eol:   lda     #0
        sta     in_pending
        lda     DFLTO                               ; as the KERNAL: the RETURN is
        cmp     #3                                  ; printed only when the output
        beq     @cr                                 ; is not the screen
        lda     #$0D
        jsr     put_petscii
        jsr     mirror_plot
@cr:    lda     #$0D
@ret:   ldx     save_x
        ldy     save_y
        clc
        rts

; take_line — the logical line of the cursor (from the start of the input,
; when the input started on it) as PETSCII in inbuf, trailing spaces cut
take_line:
        jsr     line_read
        lda     #0
        sta     in_len
        sta     quote
        sta     tmp4                                ; first cell
        lda     in_row
        bmi     @from
        ldx     cur_row                             ; did the input start on this line?
        stx     tmp2
        sta     cur_row
        jsr     line_start
        lda     tmp2
        sta     cur_row
        cpx     line_row
        bne     @from
        lda     in_row
        sec
        sbc     line_row
        tax
        lda     in_col
@pos:   dex
        bmi     @set
        clc
        adc     #COLS
        bne     @pos
@set:   sta     tmp4
@from:  ldx     line_len                            ; cut the trailing spaces
@trim:  cpx     tmp4
        beq     @copy
        txa
        asl     a
        tay
        lda     linebuf-2,y
        and     #$7F
        cmp     #' '
        bne     @copy
        dex
        bne     @trim
@copy:  stx     tmp2                                ; last cell + 1
        ldx     tmp4
@cell:  cpx     tmp2
        bcs     @done
        ldy     in_len
        cpy     #INBUF_MAX
        bcs     @done
        txa
        asl     a
        tay
        lda     linebuf,y
        jsr     screen_to_petscii
        ldy     in_len
        sta     inbuf,y
        inc     in_len
        inx
        bne     @cell
@done:  lda     #0
        sta     quote
        rts

; screen_to_petscii — A screen code to PETSCII, as the KERNAL reads its
; screen: inside quotes the reversed codes are control codes again
screen_to_petscii:
        cmp     #$22                                ; '"'
        bne     @rev
        pha
        lda     quote
        eor     #1
        sta     quote
        pla
@rev:   cmp     #$80
        bcc     @plain
        ldy     quote
        beq     @drop
        and     #$7F
        cmp     #$20
        bcc     @ret                                ; $80-$9F -> $00-$1F
        cmp     #$40
        bcc     @plain
        cmp     #$60
        bcs     @plain
        clc
        adc     #$40                                ; $C0-$DF -> $80-$9F
        rts
@drop:  and     #$7F
@plain: cmp     #$20
        bcc     @add40                              ; $00-$1F -> $40-$5F
        cmp     #$40
        bcc     @ret                                ; $20-$3F
        cmp     #$60
        bcc     @add80                              ; $40-$5F -> $C0-$DF
        clc
        adc     #$40                                ; $60-$7F -> $A0-$BF
        rts
@add80: ora     #$80
        rts
@add40: ora     #$40
@ret:   rts

; ============================================================================
; Data
; ============================================================================

        .segment "RESBSS"
code:           .res 1                  ; control code being executed
save_a:         .res 1
save_x:         .res 1
save_y:         .res 1
cur_col:        .res 1
cur_row:        .res 1
plot_col:       .res 1
plot_row:       .res 1
bg_nibble:      .res 1                  ; background colour << 4
rvs:            .res 1                  ; $80: reverse on
quote:          .res 1                  ; 1: quote mode
insert_cnt:     .res 1                  ; characters still in insert mode
last_case:      .res 1                  ; $D018 & 2 last seen
last_border:    .res 1                  ; $D020 & 15 last seen
blink_enabled:  .res 1
blink_on:       .res 1
blink_cnt:      .res 1
blink_char:     .res 1
blink_color:    .res 1
in_pending:     .res 1                  ; a line is being handed to CHRIN
in_idx:         .res 1
in_len:         .res 1
in_row:         .res 1                  ; start of the input ($FF: scrolled away)
in_col:         .res 1
line_row:       .res 1
line_nrows:     .res 1
line_len:       .res 1                  ; cells
line_bytes:     .res 1
line_pos:       .res 1
tmp:            .res 1
tmp2:           .res 1
tmp3:           .res 1
tmp4:           .res 1
link:           .res ROWS               ; 1: the row continues the line above
linebuf:        .res LINE_CELLS * 2
inbuf:          .res INBUF_MAX
