; rmtirq.s - RMT player on the C64 with the VERA PSG: raster IRQ
;
; The C64 version of rmtvbi.s. The Atari RMT player (rmtplayr.s, or
; lzsplayr.s for a .sap/.xex song, assembled with -D RMT_C64 -D RMT_VERA)
; writes its POKEY registers to pokey_shadow (RAM: the C64 has no POKEY)
; and psgrmt.s turns the channel state into VERA PSG voices.
;
; Timing as on an Atari of the same TV standard (and as the SID player of
; AT2019/ATARI-Driver/rmt_cbm64, sidrmt.s): from a raster IRQ chained to the
; KERNAL IRQ vector ($0314) the player is called once per frame, 50 times a
; second on a PAL C64 and 60 on an NTSC one, so a song plays at the speed
; it has on a PAL or NTSC Atari. The KERNAL timer IRQ (CIA 1) is 60 Hz on
; both machines and cannot be used for the tempo. With instrument speed
; N > 1 the player is called N times per frame at evenly spaced raster
; lines (every 312 / N lines on PAL, as the FASTPLAY of an RMT SAP; 262 or
; 263 / N on NTSC). After the last call of the frame psg_update writes the
; VERA PSG: the sound changes once per frame, as POKEY does with the VBI
; player on the Atari.
;
; The VERA writes happen inside the IRQ: psgrmt.s saves and restores CTRL,
; FX_CTRL and the address of port 0, so the main program can be in the
; middle of its own VRAM access sequence.
;
; The player's zero page (segment RMTZP, BASIC work area $22-$34, see
; rmt.cfg) is saved at start-up and given back at exit, so BASIC finds its
; pointers again.
;
; C interface: see rmt.h

        .export _rmt_init, _rmt_irq_on, _rmt_irq_off
        .export _rmt_frames, _rmt_lines, _rmt_maxlines, _rmt_ispeed
        .export _rmt_ntsc, _rmt_frame_lines, rmt_palnts
        .export pokey_shadow, _rmt_pokey := pokey_shadow
        .import RASTERMUSICTRACKER
        .import psg_update, psg_silence
        .import __RMTZPMEM_START__, __RMTZPMEM_SIZE__

        .constructor rmt_startup
        .destructor rmt_exit

rmt_init    = RASTERMUSICTRACKER+0      ; X/Y = module, A = song line
rmt_play    = RASTERMUSICTRACKER+3
rmt_silence = RASTERMUSICTRACKER+9

VIC_CTRL1   = $D011
VIC_RASTER  = $D012
VIC_IRQ     = $D019
VIC_IRQMASK = $D01A
CINV        = $0314                     ; KERNAL IRQ vector
IRQ_EXIT    = $EA81                     ; KERNAL: pull Y/X/A, RTI

        .segment "DATA"

; what RMTZP held before the program started; not in BSS: cc65 clears the
; BSS after running the constructors (they live in the same area)
zp_saved:       .res 32
; PAL / NTSC, set by the start-up constructor (before the BSS is cleared,
; so not in BSS either)
_rmt_ntsc:      .byte 0         ; 1 = NTSC machine (60 Hz frames)
rmt_palnts:     .byte 1         ; the Atari PALNTS: 1 = PAL, 0 = NTSC (psgrmt.s)
_rmt_frame_lines: .word 312     ; raster lines of a frame: 312, 262 or 263

        .segment "BSS"

pokey_shadow:   .res 16         ; the POKEY registers written by the player
_rmt_frames:    .res 2          ; frames played
_rmt_lines:     .res 1          ; raster lines of the last frame's music
_rmt_maxlines:  .res 1          ; the worst of them
_rmt_ispeed:    .res 1          ; player calls per frame (instrument speed, 1..4)
on:             .res 1          ; the IRQ is installed
old_irq:        .res 2
tick:           .res 1          ; call of the frame the next raster IRQ makes
line_lo:        .res 4          ; raster line of each call of a frame
line_hi:        .res 4          ; its bit 8 (0 / 1)
start_lo:       .res 1          ; raster line at the start of the frame's music
start_hi:       .res 1
tmp:            .res 1
dvd:            .res 2
rem:            .res 2

        .segment "CODE"

; ---------------------------------------------------------------------------
; start-up: save the player's zero page, PAL / NTSC from the raster lines
rmt_startup:
        ldx #<(__RMTZPMEM_SIZE__ - 1)
@l:     lda __RMTZPMEM_START__,x
        sta zp_saved,x
        dex
        bpl @l
        ; the low byte of the last raster line is $37 on PAL (312 lines),
        ; $05/$06 on NTSC (262/263 lines) (codebase64)
        php
        sei
@l1:    lda VIC_RASTER
@l2:    cmp VIC_RASTER
        beq @l2
        bmi @l1
        plp
        sta _rmt_frame_lines            ; last line + 1
        inc _rmt_frame_lines
        ldx #1
        stx _rmt_frame_lines+1
        ldy #0                          ; Y = NTSC flag
        cmp #$20
        bcs @pal
        iny
        dex                             ; X = PALNTS
@pal:   sty _rmt_ntsc
        stx rmt_palnts
        rts

; exit: music off, zero page given back
rmt_exit:
        jsr _rmt_irq_off
        ldx #<(__RMTZPMEM_SIZE__ - 1)
@l:     lda zp_saved,x
        sta __RMTZPMEM_START__,x
        dex
        bpl @l
        rts

; ---------------------------------------------------------------------------
; unsigned char __fastcall__ rmt_init(const void *module)
; Only while the IRQ is off. Returns the instrument speed (1..4).
_rmt_init:
        pha                             ; X = lo, Y = hi for the player
        txa
        tay
        pla
        tax
        lda #0                          ; start from song line 0
        jsr rmt_init                    ; A = instrument speed
        cmp #1                          ; clamp to 1..4 (RMT format range)
        bcs @lo
        lda #1
@lo:    cmp #5
        bcc @hi
        lda #4
@hi:    sta _rmt_ispeed
        ; the raster lines of the calls of a frame: k * (lines / ispeed)
        lda _rmt_frame_lines
        sta rem
        lda _rmt_frame_lines+1
        sta rem+1
        lda #0
        sta dvd
        sta dvd+1
@div:   lda rem
        sec
        sbc _rmt_ispeed
        tax
        lda rem+1
        sbc #0
        bcc @step
        sta rem+1
        stx rem
        inc dvd
        bne @div
        inc dvd+1
        bne @div
@step:  lda #0                          ; call 0: the top of the frame
        sta rem
        sta rem+1
        ldx #0
@ln:    lda rem
        sta line_lo,x
        lda rem+1                       ; bit 8 of the line
        sta line_hi,x
        clc
        lda rem
        adc dvd
        sta rem
        lda rem+1
        adc dvd+1
        sta rem+1
        inx
        cpx _rmt_ispeed
        bcc @ln
        lda _rmt_ispeed
        ldx #0
        rts

; ---------------------------------------------------------------------------
; void rmt_irq_on(void): raster IRQ at line 0 (and at the lines of the other
; player calls with instrument speed > 1), chained to the KERNAL one
_rmt_irq_on:
        php
        sei
        lda on
        bne @done
        lda _rmt_ispeed                 ; not initialised: one call per frame
        bne @sp
        lda #1
        sta _rmt_ispeed
        lda #0
        sta line_lo
        sta line_hi
@sp:    lda CINV
        sta old_irq
        lda CINV+1
        sta old_irq+1
        lda #<irq
        sta CINV
        lda #>irq
        sta CINV+1
        lda #0
        sta tick
        sta _rmt_maxlines
        sta VIC_RASTER
        lda VIC_CTRL1
        and #$7F                        ; raster line bit 8 = 0
        sta VIC_CTRL1
        lda #$01
        sta VIC_IRQ                     ; clear a pending raster IRQ
        sta VIC_IRQMASK                 ; enable the raster IRQ
        sta on
@done:  plp
        rts

; void rmt_irq_off(void): IRQ removed, PSG voices silent (the song position
; is kept: rmt_irq_on goes on from there)
_rmt_irq_off:
        php
        sei
        lda on
        beq @done
        lda #0
        sta VIC_IRQMASK
        sta on
        lda #$01
        sta VIC_IRQ
        lda old_irq
        sta CINV
        lda old_irq+1
        sta CINV+1
        jsr rmt_silence
        jsr psg_silence
@done:  plp
        rts

; ---------------------------------------------------------------------------
; KERNAL IRQ entry has pushed A, X, Y
irq:
        lda VIC_IRQ
        and #$01
        bne @tick
        jmp (old_irq)                   ; CIA timer: keyboard, clock
@tick:  lda #$01
        sta VIC_IRQ                     ; acknowledge the raster IRQ
        ldx tick
        bne @play                       ; frames: counted at the first call
        inc _rmt_frames
        bne @t0
        inc _rmt_frames+1
@t0:    jsr raster                      ; the music of the frame starts
        sta start_lo
        sty start_hi
@play:  jsr rmt_play                    ; writes pokey_shadow, computes next frame
        ldx tick
        inx
        cpx _rmt_ispeed
        bcc @next                       ; not the last call of the frame
        jsr psg_update                  ; channel state -> VERA PSG voices
        jsr timing
        ldx #0
@next:  stx tick
        lda line_lo,x                   ; raster IRQ of the next call
        sta VIC_RASTER
        lda line_hi,x
        lsr a                           ; C = bit 8 of the line
        lda VIC_CTRL1
        and #$7F
        bcc @h8
        ora #$80
@h8:    sta VIC_CTRL1
        txa
        beq @x                          ; call 0: line 0 of the next frame
        ; the line may be behind already (a long call): make the call now,
        ; otherwise the IRQ would come one frame late and the tempo slip
        jsr raster
        sta tmp
        tya
        cmp line_hi,x
        bcc @x                          ; before the line (bit 8 lower)
        bne @play                       ; past it (bit 8 higher)
        lda tmp
        cmp line_lo,x
        bcs @play                       ; at or past the line
@x:     jmp IRQ_EXIT

; A = raster line bits 7:0, Y = bit 8 (0 / 1); preserves X
raster:
@rd:    lda VIC_RASTER
        ldy VIC_CTRL1
        cmp VIC_RASTER
        bne @rd
        cpy #$80                        ; C = bit 8
        ldy #0
        bcc @r
        iny
@r:     rts

; _rmt_lines = raster lines from the start of the frame's music to now
; (player calls, also the ones spread over the frame, and psg_update)
timing:
        jsr raster
        sec
        sbc start_lo
        sta tmp
        tya
        sbc start_hi
        bcs @pos                        ; now >= start
        tax                             ; wrapped past the end of the frame:
        lda tmp                         ; add the lines of a frame
        clc
        adc _rmt_frame_lines
        sta tmp
        txa
        adc _rmt_frame_lines+1
@pos:   cmp #0
        beq @lo
        lda #255                        ; more than 255 lines: clamp
        sta tmp
@lo:    lda tmp
        sta _rmt_lines
        cmp _rmt_maxlines
        bcc @r
        sta _rmt_maxlines
@r:     rts
