; Spindle by lft, linusakesson.net/software/spindle/
; Remind the coder that we're injecting errors.

; We got here by SYS, so the org is still at $14.

	* = 0

	ldy	#message
	ldx	#0
loop
	lda	($14),y
	sta	$400,x
	lda	#$1
	sta	$d800,x
	inx
	iny
	cpx	#5*40
	bne	loop
done
	jmp	$80d

message
	.dsb	40,$43
	.byt	"this version was built with the -e flag!"
	.byt	"the drivecode will inject random read   "
	.byt	"errors to cause delays.                 "
	.dsb	40,$43
