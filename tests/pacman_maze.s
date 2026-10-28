; pacman_maze.s - the maze bitmap of pacman.c, fast: for every row of
; the picture (pac_maze_src: its maze row) the 28 tiles of that maze row,
; one 2 bpp line of each turned into 4 bytes of the 4 bpp bitmap (the arcade
; colours), then 48 zeros to the end of the 160 bytes of the row, so that
; the whole bitmap is written in one go.  Tables made once by the C side:
;
;   mr_rlo, mr_rhi  the address of the 28 tile codes of every screen tile
;                   row (mrow)
;   mr_tlo, mr_thi  the address of every tile in pac_tiles
;   mr_maze_a, _b   a 2 bpp byte (4 pixels) -> its 2 bytes of the bitmap, in
;                   the colours of the walls
;   mr_door_a, _b   the same for the door of the ghost house ($CE, $CF)
;
; The VERA address is the start of the bitmap, increment 1.  A blank tile
; ($40) is 4 zeros; any other about 60 cycles.

.include "vera_common.inc"

.importzp ptr1, ptr2, tmp1, tmp2
.import _pac_maze_src
.export _maze_draw, _mr_rlo, _mr_rhi, _mr_tlo, _mr_thi
.export _mr_maze_a, _mr_maze_b, _mr_door_a, _mr_door_b

T_BLANK = $40
T_DOOR1 = $CE
T_DOOR2 = $CF
MAZE_SHOWN = 240
MAZE_TOP_ROW = 3                        ; the screen tile row of maze row 0

.bss
_mr_rlo:        .res    36
_mr_rhi:        .res    36
mr_line:        .res    1
_mr_tlo:        .res    256
_mr_thi:        .res    256
_mr_maze_a:     .res    256
_mr_maze_b:     .res    256
_mr_door_a:     .res    256
_mr_door_b:     .res    256

.code
_maze_draw:
        lda     #0
        sta     tmp2                    ; the row of the picture
@row:   ldx     tmp2
        lda     _pac_maze_src, x        ; its maze row m
        tax
        and     #7                      ; the line, times 2
        asl     a
        sta     mr_line
        txa                             ; the screen tile row
        lsr     a
        lsr     a
        lsr     a
        clc
        adc     #MAZE_TOP_ROW
        tax
        lda     _mr_rlo, x
        sta     ptr1
        lda     _mr_rhi, x
        sta     ptr1 + 1
        jsr     maze_row
        lda     #0                      ; x 224-319
        ldy     #48
:       sta     VERA_DATA0
        dey
        bne     :-
        inc     tmp2
        lda     tmp2
        cmp     #MAZE_SHOWN
        bne     @row
        rts

; the 28 tiles at ptr1, line mr_line
maze_row:
        ldy     #0

@tile:  sty     tmp1
        lda     (ptr1), y               ; the tile code
        cmp     #T_BLANK
        bne     @full
        lda     #0
        sta     VERA_DATA0
        sta     VERA_DATA0
        sta     VERA_DATA0
        sta     VERA_DATA0
        jmp     @next

@full:  tax                             ; ptr2: its line in pac_tiles
        lda     _mr_tlo, x
        clc
        adc     mr_line
        sta     ptr2
        lda     _mr_thi, x
        adc     #0
        sta     ptr2 + 1
        cpx     #T_DOOR1
        beq     @door
        cpx     #T_DOOR2
        beq     @door

        ldy     #0
        lda     (ptr2), y
        tax
        lda     _mr_maze_a, x
        sta     VERA_DATA0
        lda     _mr_maze_b, x
        sta     VERA_DATA0
        iny
        lda     (ptr2), y
        tax
        lda     _mr_maze_a, x
        sta     VERA_DATA0
        lda     _mr_maze_b, x
        sta     VERA_DATA0
        jmp     @next

@door:  ldy     #0
        lda     (ptr2), y
        tax
        lda     _mr_door_a, x
        sta     VERA_DATA0
        lda     _mr_door_b, x
        sta     VERA_DATA0
        iny
        lda     (ptr2), y
        tax
        lda     _mr_door_a, x
        sta     VERA_DATA0
        lda     _mr_door_b, x
        sta     VERA_DATA0

@next:  ldy     tmp1
        iny
        cpy     #28
        bne     @tile
        rts
