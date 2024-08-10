; Spindle by lft, linusakesson.net/software/spindle/

; Block the restore key (unblock at any time by reading dd0d).
; Initialise d01a and dc02.
; Prepare CIA #1 timer B to compensate for interrupt jitter.

; This code is inlined into the first effect driver by pefchain, and is also
; used by pef2prg. Use option -s with either of these programs to replace it.
; Code must be able to run from any location, but won't cross a page boundary.
; Maximum size 128 bytes.

; The code in this file is in the public domain.

	bit	$d011
	bmi	*-3

	bit	$d011
	bpl	*-3

	lda	#$40		; rti
	sta	$ffff
	ldx	#$ff
	stx	$fffa
	stx	$fffb
	inx
	stx	$dd0e
	stx	$dd04
	stx	$dd05
	lda	#$81
	sta	$dd0d
	lda	#$19
	sta	$dd0e

	ldx	$d012
	inx
resync
	cpx	$d012
	bne	*-3
	; at cycle 4 or later
	ldy	#0		; 4
	sty	$dc07		; 6
	lda	#62		; 10
	sta	$dc06		; 12
	iny			; 16
	sty	$d01a		; 18
	dey			; 22
	dey			; 24
	sty	$dc02		; 26
	nop			; 30
	nop			; 32
	nop			; 34
	nop			; 36
	nop			; 38
	nop			; 40
	nop			; 42
	nop			; 44
	nop			; 46
	lda	#$11		; 48
	sta	$dc0f		; 50
	txa			; 54
	inx			; 56
	inx			; 58
	cmp	$d012		; 60	still on the same line?
	bne	resync
