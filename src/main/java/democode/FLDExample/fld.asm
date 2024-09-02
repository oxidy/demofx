
.pc = $9400 "DATA: FLD-code"

fld:
        lda START_Y
        cmp $d012
        bne *-3

zz:     ldy #$00
        lda data,y
        jsr fld_loop
        inc zz+1
        lda zz+1
        cmp #$46
        bne !+
        lda #$00
        sta zz+1

!:      lda $d012
        clc
        adc #$20
        cmp $d012
        bne *-3

        rts

        // -----------------

fld_loop:
        beq *-1
        tax
!:      lda $d012
        cmp $d012
        beq *-3
        clc
        lda $d011
        adc #1
        and #7
        ora #$38
        sta $d011
        dex
        bne !-
        rts

.pc = $9e00 "DATA: Params"

START_Y:
        .byte $50
        
.pc = $9f00 "DATA: Sinus"
data:
    .byte $00,$00,$00,$00,$00,$00,$00
    .byte $01,$01,$01,$01
    .byte $02,$02,$02
    .byte $03,$03
    .byte $04,$04
    .byte $05,$06,$07,$08,$09,$0a
    .byte $0b,$0b
    .byte $0c,$0c
    .byte $0d,$0d,$0d
    .byte $0e,$0e,$0e,$0e
    .byte $0f,$0f,$0f,$0f,$0f,$0f,$0f
    .byte $0e,$0e,$0e,$0e
    .byte $0d,$0d,$0d
    .byte $0c,$0c
    .byte $0b,$0b
    .byte $0a,$09,$08,$07,$06,$05
    .byte $04,$04
    .byte $03,$03
    .byte $02,$02,$02
    .byte $01,$01,$01,$01
    .byte $00,$00,$00,$00,$00,$00,$00

