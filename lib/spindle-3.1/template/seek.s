; Spindle by lft, www.linusakesson.net/software/spindle/

; Starting with Spindle 3.0, the low-level Spin API lets you seek to any label
; in the script using this routine.

; Pefchain uses a different mechanism for seeking (see the documentation).

; The code in this file is in the public domain.

spin_seek
	; A = desired seek point, 00-3f

	ora	#$80
	sec
	rol
	ldx	#$18

spin_seek_bitloop
	stx	$dd00		; Pull clock and atn.

	bcc	*+4
	ldy	#$10		; Y becomes 00 or 10 according to bit.

	bit	$dd00		; Wait for the drive to become ready.
	bpl	*-3

	sty	$dd00		; Release atn; clock carries the bit.

	ldy	#5		; Give the drive enough time to read the bit
	dey			; and to pull the data line again.
	bne	*-1

	asl
	bne	spin_seek_bitloop

	ldx	#$08
	stx	$dd00		; Pull atn after the last bit.

	rts
