; ============================================================================
; vera_irq.s - VERA IRQ hook on the KERNAL IRQ vector CINV ($0314)
;
; See vera_irq.h for the rationale and the C interface.
;
; KERNAL contract: the IRQ entry ($FF48) pushes A, X and Y and jumps through
; CINV with the stack as  Y X A P PCL PCH.  To pass the IRQ on, JMP (old
; CINV) with the stack untouched; to end it here, JMP $EA81 (pull Y, X, A
; and RTI).
;
; The VERA /IRQ is wired-OR with CIA 1 on the 6510 /IRQ line.  When the
; VERA has a source pending the hook services it and ends the IRQ without
; the KERNAL handler: the jiffy clock and the keyboard scan run only on the
; CIA 1 timer IRQ.  If CIA 1 was also asserting, the line is still low after
; the RTI and the IRQ is taken again at once: the VERA has nothing pending
; then, and the hook chains to the KERNAL.
; ============================================================================

    .setcpu "6502"
    .include "vera_common.inc"

CINV            = $0314                 ; KERNAL IRQ vector (RAM)
IRQ_EXIT        = $EA81                 ; KERNAL: pla tay pla tax pla rti
IRQ_MASK        = $0F                   ; IEN/ISR bits 3:0 = AFLOW|SPRCOL|LINE|VSYNC

    .export _vera_irq_install
    .export _vera_irq_remove
    .export _vera_irq_enable
    .export _vera_irq_disable
    .export _vera_irq_take
    .export _vera_irq_set_line
    .export _vera_irq_set_callback
    .export _vera_irq_count

    .segment "BSS"

old_cinv:       .res 2
callback:       .res 2
installed:      .res 1
flags:          .res 1
pending:        .res 1
tmp:            .res 1
tmp2:           .res 1
_vera_irq_count: .res 4

    .segment "CODE"

; ----------------------------------------------------------------------------
; The IRQ handler (A, X, Y already saved by the KERNAL)
; ----------------------------------------------------------------------------
irq_handler:
    lda VERA_ISR
    and VERA_IEN
    and #IRQ_MASK
    bne @ours
    jmp (old_cinv)              ; not the VERA: let the KERNAL handle it

@ours:
    sta pending
    and #$07
    beq @aflow
    sta VERA_ISR                ; ack VSYNC/LINE/SPRCOL (write 1 clears only these)
@aflow:
    lda pending
    and #$08
    beq @record
    lda VERA_IEN                ; AFLOW cannot be cleared: stop the level IRQ
    and #$F7
    sta VERA_IEN
@record:
    lda pending
    ora flags
    sta flags

    lda pending                 ; per-source counters
    lsr a
    bcc @c1
    inc _vera_irq_count+0
@c1:
    lsr a
    bcc @c2
    inc _vera_irq_count+1
@c2:
    lsr a
    bcc @c3
    inc _vera_irq_count+2
@c3:
    lsr a
    bcc @cb
    inc _vera_irq_count+3
@cb:
    lda callback+1
    beq @done
    lda pending                 ; A = serviced sources
    jsr call_cb
@done:
    jmp IRQ_EXIT

call_cb:
    jmp (callback)

; ----------------------------------------------------------------------------
; void vera_irq_install(void)
; ----------------------------------------------------------------------------
_vera_irq_install:
    lda installed
    bne @done
    php
    sei
    lda CINV
    sta old_cinv
    lda CINV+1
    sta old_cinv+1
    lda #0
    sta flags
    sta callback
    sta callback+1
    ldx #3
@clr:
    sta _vera_irq_count,x
    dex
    bpl @clr
    lda #<irq_handler
    sta CINV
    lda #>irq_handler
    sta CINV+1
    lda #1
    sta installed
    plp
@done:
    rts

; ----------------------------------------------------------------------------
; void vera_irq_remove(void)
; ----------------------------------------------------------------------------
_vera_irq_remove:
    lda installed
    beq @done
    php
    sei
    lda VERA_IEN                ; all four sources off, keep IRQ_LINE[8]
    and #$80
    sta VERA_IEN
    lda CINV                    ; restore only if the vector is still ours
    cmp #<irq_handler
    bne @keep
    lda CINV+1
    cmp #>irq_handler
    bne @keep
    lda old_cinv
    sta CINV
    lda old_cinv+1
    sta CINV+1
    lda #0
    sta installed
@keep:                          ; someone hooked after us: stay (now inert, IEN = 0)
    plp
@done:
    rts

; ----------------------------------------------------------------------------
; void __fastcall__ vera_irq_enable(unsigned char mask)    A = mask
; ----------------------------------------------------------------------------
_vera_irq_enable:
    and #IRQ_MASK
    sta tmp
    php
    sei
    lda tmp
    and #$07
    sta VERA_ISR                ; drop stale status of the sources being enabled
    lda VERA_IEN
    and #$8F                    ; keep IRQ_LINE[8] and the current enables
    ora tmp
    sta VERA_IEN
    plp
    rts

; ----------------------------------------------------------------------------
; void __fastcall__ vera_irq_disable(unsigned char mask)   A = mask
; ----------------------------------------------------------------------------
_vera_irq_disable:
    and #IRQ_MASK
    eor #$FF                    ; bits 7:4 stay 1: IRQ_LINE[8] preserved
    sta tmp
    php
    sei
    lda VERA_IEN
    and tmp
    sta VERA_IEN
    plp
    rts

; ----------------------------------------------------------------------------
; unsigned char vera_irq_take(void)   returns and clears the serviced flags
; ----------------------------------------------------------------------------
_vera_irq_take:
    php
    sei
    lda flags
    ldx #0
    stx flags
    plp
    rts

; ----------------------------------------------------------------------------
; void __fastcall__ vera_irq_set_line(unsigned int line)   A = low, X = high
; ----------------------------------------------------------------------------
_vera_irq_set_line:
    sta tmp
    txa
    and #$01
    sta tmp2
    php
    sei
    lda tmp
    sta VERA_IRQLINE_L
    lda VERA_IEN
    and #IRQ_MASK               ; current enables, IRQ_LINE[8] rebuilt below
    ldx tmp2
    beq @b8
    ora #$80
@b8:
    sta VERA_IEN
    plp
    rts

; ----------------------------------------------------------------------------
; void __fastcall__ vera_irq_set_callback(void (*cb)(void))  A = low, X = high
; ----------------------------------------------------------------------------
_vera_irq_set_callback:
    php
    sei
    sta callback
    stx callback+1
    plp
    rts
