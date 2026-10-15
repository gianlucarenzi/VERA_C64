; vtm_player.s — VTM ("Vera Tracker Module") playback engine.
;
; Drives 4 of VERA's 16 PSG wavetable voices from a compiled .vtm blob
; (see vtm_format.md). Call _vtm_tick() once per frame — e.g. from the
; host's own jiffy-poll loop, the same way test_matrix.c drives its own
; per-frame update. No IRQ handler is installed here: playback is
; host-driven.
;
; Ported from the Atari PBI card: no PBI latch to select, no CRITIC.
;
; C API (see vtm.h):
;   unsigned char vtm_init(const void *song);   1 = ok, 0 = bad/unsupported file
;   void          vtm_tick(void);               call once per frame
;   void          vtm_stop(void);                silence all channels
;   unsigned char vtm_level(unsigned char ch);   current volume (0-63) of channel ch

    .setcpu "6502"

    .export _vtm_init, _vtm_tick, _vtm_stop, _vtm_level

    .include "vera_common.inc"

N_CHANNELS   = 4
NOTE_HOLD    = 0
NOTE_OFF_VAL = 97
NO_INSTR     = $FF

; PSG voice base address low byte, per channel (mid byte and bank are
; constant for all 4 — see PSG_ADDR_M/PSG_ADDR_H below).
PSG_ADDR_M   = >VERA_PSG_BASE                ; $F9 (VRAM address bits 15:8)
PSG_ADDR_H   = (VERA_INC1 | ^VERA_PSG_BASE)  ; bank=1, auto-increment +1

; ============================================================================
; Zero page — indirect pointers only. Everything else lives in BSS.
; ============================================================================

    .segment "ZEROPAGE"

zp_song:    .res 2          ; base of the loaded .vtm blob
zp_order:   .res 2          ; -> order[]
zp_instr:   .res 2          ; -> instruments[]
zp_pattbl:  .res 2          ; -> pattern_table[]
zp_row:     .res 2          ; -> current row's cell data

; ============================================================================
; Player state
; ============================================================================

    .segment "BSS"

vtm_active:          .res 1
vtm_frames_per_row:  .res 1
vtm_frame_ctr:       .res 1
vtm_order_len:       .res 1
vtm_loop_pos:        .res 1
vtm_order_idx:       .res 1
vtm_row_left:        .res 1
vtm_chan_instr:      .res 4          ; last-set instrument index per channel
vtm_chan_vol:        .res 4          ; live reg2 (pan+volume) — decays over time
vtm_chan_decay:      .res 4          ; per-frame volume decrement; 0 = sustain (no envelope)

cur_ch:              .res 1
cur_note:            .res 1
cur_instr:           .res 1
cur_pattern:         .res 1
tmp_lo:              .res 1
tmp_hi:              .res 1
freq_lo:             .res 1
freq_hi:             .res 1
n_instr_tmp:         .res 1

    .segment "RODATA"

psg_addr_l:
    .byte <(VERA_PSG_BASE + 0*VERA_PSG_VOICE_SIZE)
    .byte <(VERA_PSG_BASE + 1*VERA_PSG_VOICE_SIZE)
    .byte <(VERA_PSG_BASE + 2*VERA_PSG_VOICE_SIZE)
    .byte <(VERA_PSG_BASE + 3*VERA_PSG_VOICE_SIZE)

    .include "vtm_notes.inc"

    .segment "CODE"

; ============================================================================
; _vtm_init(song) — A/X = pointer to a loaded .vtm blob (cc65 default
; calling convention: sole pointer argument in A(lo)/X(hi)).
; Returns 1 (A) on success, 0 on bad magic / unsupported channel count.
; ============================================================================

_vtm_init:
    sta zp_song
    stx zp_song+1

    ldy #0
    lda (zp_song),y
    cmp #$56                         ; 'V' (ASCII: ca65 -t c64 maps 'V' to PETSCII)
    bne @bad
    iny
    lda (zp_song),y
    cmp #$54                         ; 'T'
    bne @bad
    iny
    lda (zp_song),y
    cmp #$4D                         ; 'M'
    bne @bad
    iny
    lda (zp_song),y
    cmp #$33                         ; '3'
    bne @bad

    ldy #4                          ; n_channels
    lda (zp_song),y
    cmp #N_CHANNELS
    beq @magic_ok
@bad:
    jmp @fail
@magic_ok:

    ldy #5                          ; frames_per_row
    lda (zp_song),y
    sta vtm_frames_per_row
    lda #1                          ; fire the first row on the very next tick
    sta vtm_frame_ctr

    ldy #8                          ; order_len
    lda (zp_song),y
    sta vtm_order_len
    ldy #9                          ; loop_pos
    lda (zp_song),y
    sta vtm_loop_pos

    ldy #12                         ; title_len
    lda (zp_song),y
    sta tmp_lo                      ; stash title_len (title text itself is
                                     ; never read by the player, only skipped)

    ; zp_order = zp_song + 13 (header size) + title_len
    clc
    lda zp_song
    adc #13
    sta zp_order
    lda zp_song+1
    adc #0
    sta zp_order+1
    clc
    lda zp_order
    adc tmp_lo
    sta zp_order
    lda zp_order+1
    adc #0
    sta zp_order+1

    ; zp_instr = zp_order + order_len
    clc
    lda zp_order
    adc vtm_order_len
    sta zp_instr
    lda zp_order+1
    adc #0
    sta zp_instr+1

    ; zp_pattbl = zp_instr + n_instruments*3 (instruments are 3 bytes each:
    ; reg2, reg3, decay). Computed as n_instr*2 (16-bit safe) + n_instr.
    ldy #6                          ; n_instruments
    lda (zp_song),y
    sta n_instr_tmp
    asl a
    sta tmp_lo
    lda #0
    rol a                           ; capture the 9th bit of n_instruments*2
    sta tmp_hi
    clc
    lda tmp_lo
    adc n_instr_tmp
    sta tmp_lo
    lda tmp_hi
    adc #0
    sta tmp_hi
    clc
    lda zp_instr
    adc tmp_lo
    sta zp_pattbl
    lda zp_instr+1
    adc tmp_hi
    sta zp_pattbl+1

    lda #NO_INSTR
    sta vtm_chan_instr+0
    sta vtm_chan_instr+1
    sta vtm_chan_instr+2
    sta vtm_chan_instr+3
    lda #0
    sta vtm_chan_decay+0
    sta vtm_chan_decay+1
    sta vtm_chan_decay+2
    sta vtm_chan_decay+3
    sta vtm_chan_vol+0
    sta vtm_chan_vol+1
    sta vtm_chan_vol+2
    sta vtm_chan_vol+3

    lda #0
    sta vtm_order_idx
    jsr clamp_and_load_pattern

    lda #1
    sta vtm_active
    lda #1
    rts

@fail:
    lda #0
    sta vtm_active
    lda #0
    rts

; ============================================================================
; _vtm_tick — call once per frame. No-op if not playing or mid-row.
; ============================================================================

_vtm_tick:
    lda vtm_active
    bne @go
    rts
@go:
    lda #0
    sta VERA_CTRL

    jsr envelope_tick                ; runs every frame, not just on row edges

    dec vtm_frame_ctr
    bne @rowdone
    lda vtm_frames_per_row
    sta vtm_frame_ctr
    jsr play_row

    clc                              ; advance to next row's cells
    lda zp_row
    adc #(N_CHANNELS*2)
    sta zp_row
    bcc @noc
    inc zp_row+1
@noc:
    dec vtm_row_left
    bne @rowdone
    jsr advance_order
@rowdone:
    rts

; ============================================================================
; _vtm_stop — silence all channels and halt playback.
; ============================================================================

_vtm_stop:
    lda #0
    sta vtm_active

    lda #0
    sta VERA_CTRL
    ldx #0
@loop:
    lda psg_addr_l,x
    clc
    adc #2                          ; +2 = pan/volume register
    sta VERA_ADDR_L
    lda #PSG_ADDR_M
    sta VERA_ADDR_M
    lda #PSG_ADDR_H
    sta VERA_ADDR_H
    lda #0
    sta VERA_DATA0
    inx
    cpx #N_CHANNELS
    bne @loop
    rts

; ============================================================================
; _vtm_level(ch) — A = channel 0-3 (cc65 default calling convention: sole
; byte argument in A). Returns the channel's current live volume (0-63,
; pan bits masked off) — e.g. for a VU meter. No VERA access needed,
; vtm_chan_vol is plain RAM kept up to date by envelope_tick/write_note_on.
; ============================================================================

_vtm_level:
    tax
    lda vtm_chan_vol,x
    and #$3F
    rts

; ============================================================================
; play_row — apply the current row's cell for each channel. Caller has
; already set VERA_CTRL=0.
; ============================================================================

play_row:
    lda #0
    sta cur_ch
@chloop:
    lda cur_ch
    asl a
    tay
    lda (zp_row),y
    sta cur_note
    iny
    lda (zp_row),y
    sta cur_instr

    lda cur_note
    beq @next                       ; hold — nothing to do
    cmp #NOTE_OFF_VAL
    beq @do_off

    lda cur_instr
    cmp #NO_INSTR
    beq @have_instr
    ldx cur_ch
    lda cur_instr
    sta vtm_chan_instr,x
@have_instr:
    jsr write_note_on
    jmp @next

@do_off:
    jsr write_note_off

@next:
    inc cur_ch
    lda cur_ch
    cmp #N_CHANNELS
    bne @chloop
    rts

; ============================================================================
; write_note_on — cur_ch/cur_note valid; retrigger the channel's voice with
; its current instrument's timbre and cur_note's frequency.
; ============================================================================

write_note_on:
    ldx cur_ch
    lda vtm_chan_instr,x
    cmp #NO_INSTR
    bne @haveidx
    rts                              ; note-on with no instrument ever set — ignore
@haveidx:
    sta tmp_hi                       ; stash instrument index
    asl a                            ; instrument index * 3 (3 bytes/instrument)
    clc
    adc tmp_hi
    tay
    lda (zp_instr),y                 ; reg2 (pan/volume) — becomes the live envelope state
    ldx cur_ch
    sta vtm_chan_vol,x
    pha
    iny
    lda (zp_instr),y                 ; reg3 (waveform/pulse width)
    sta tmp_lo
    iny
    lda (zp_instr),y                 ; decay (0 = sustain, no envelope)
    ldx cur_ch
    sta vtm_chan_decay,x

    lda cur_note
    sec
    sbc #1                           ; note index 0..95
    asl a                            ; *2 (word table)
    tay
    lda vtm_note_freq,y
    sta freq_lo
    lda vtm_note_freq+1,y
    sta freq_hi

    ldy cur_ch
    lda psg_addr_l,y
    sta VERA_ADDR_L
    lda #PSG_ADDR_M
    sta VERA_ADDR_M
    lda #PSG_ADDR_H
    sta VERA_ADDR_H

    lda freq_lo
    sta VERA_DATA0
    lda freq_hi
    sta VERA_DATA0
    pla
    sta VERA_DATA0                   ; reg2
    lda tmp_lo
    sta VERA_DATA0                   ; reg3
    rts

; ============================================================================
; write_note_off — cur_ch valid; silence the channel (volume = 0).
; ============================================================================

write_note_off:
    ldx cur_ch
    lda #0
    sta vtm_chan_decay,x             ; stop any envelope_tick from re-touching this voice
    sta vtm_chan_vol,x
    lda psg_addr_l,x
    clc
    adc #2
    sta VERA_ADDR_L
    lda #PSG_ADDR_M
    sta VERA_ADDR_M
    lda #PSG_ADDR_H
    sta VERA_ADDR_H
    lda #0
    sta VERA_DATA0
    rts

; ============================================================================
; envelope_tick — called every frame (not just on row edges). Decays any
; channel with a non-zero vtm_chan_decay by that amount, clamped at 0, and
; rewrites reg2 (pan/volume) to hardware. Self-disabling: once a channel's
; volume reaches 0 its decay is cleared too, so it's skipped on later frames
; without needing a separate "active" flag. Caller has already set
; VERA_CTRL=0.
; ============================================================================

envelope_tick:
    ldx #0
@loop:
    lda vtm_chan_decay,x
    beq @skip

    lda vtm_chan_vol,x
    and #$3F                         ; isolate volume bits (pan is 7:6)
    sec
    sbc vtm_chan_decay,x
    bcs @noclamp
    lda #0                           ; underflowed past zero — clamp
@noclamp:
    sta tmp_lo                       ; new volume 0-63
    bne @stillon
    lda #0
    sta vtm_chan_decay,x             ; fully decayed — stop touching this voice
@stillon:
    lda vtm_chan_vol,x
    and #$C0                         ; keep pan bits
    ora tmp_lo
    sta vtm_chan_vol,x

    lda psg_addr_l,x
    clc
    adc #2
    sta VERA_ADDR_L
    lda #PSG_ADDR_M
    sta VERA_ADDR_M
    lda #PSG_ADDR_H
    sta VERA_ADDR_H
    lda vtm_chan_vol,x
    sta VERA_DATA0
@skip:
    inx
    cpx #N_CHANNELS
    bne @loop
    rts

; ============================================================================
; advance_order / clamp_and_load_pattern — move to the next order position
; (wrapping to loop_pos past the end) and load that pattern's row count and
; row-data pointer.
; ============================================================================

advance_order:
    inc vtm_order_idx
    ; fall through

clamp_and_load_pattern:
    lda vtm_order_idx
    cmp vtm_order_len
    bcc @ok
    lda vtm_loop_pos
    sta vtm_order_idx
@ok:
    ldy vtm_order_idx
    lda (zp_order),y
    sta cur_pattern

    asl a                            ; pattern * 2
    clc
    adc cur_pattern                  ; + pattern = pattern * 3 (pattern_table entry size)
    tay
    lda (zp_pattbl),y
    sta vtm_row_left                 ; n_rows
    iny
    lda (zp_pattbl),y
    sta tmp_lo                       ; data_offset lo
    iny
    lda (zp_pattbl),y
    sta tmp_hi                       ; data_offset hi

    clc
    lda zp_song
    adc tmp_lo
    sta zp_row
    lda zp_song+1
    adc tmp_hi
    sta zp_row+1
    rts
