; Spindle by lft, linusakesson.net/software/spindle/

; This is uploaded to $400 in the drive
; (without knowing the drive model yet)

	* = $400

	; job code d0 entry point

	jmp	($fffc)

	; normal entry point

	ldx	#$7f	; disable interrupts

	lda	$e5c6
	cmp	#'4'
	beq	was1541

	cmp	#'7'
	bne	not157x

	; 1570 or 1571 detected

	stx	$400d

was1541
	; 1541, 1541-II, 1570, or 1571 detected

	.(
	sei

	lda	#$d0	; job code, execute buffer
	sta	1

	stx	$180e
	stx	$180d
	stx	$1c0e
	stx	$1c0d

	ldx	#$01
	stx	$1c0b	; timer 1 one-shot, latch port a
	dex
	lda	#$c0	; enable timer 1 interrupt
	sta	$1c0e

	lda	#$10
	sta	$1800

	ldy	#64

preloop
	bit	$1800
	bmi	*-3
	stx	$1800

	bit	$1800
	bpl	*-3
	sta	$1800

	dey
	bpl	preloop

loop
	bit	$1800
	bmi	*-3
	stx	$1800

	sty	$1c05	; reload timer, clear irq
	cli

	bit	$1800
	bpl	*-3
	sta	$1800

	sei
	bmi	loop	; always
	.)

not157x
	lda	$a6e9
	eor	#'8'
	bne	not1581

	; 1581 detected

	.(
	sei
	sta	$4001	; turn off auto-ack

	; wait for long atn
	; then wait for long !atn
	; then reset

	ldy	#$ff
	sty	$4005
	ldx	#$19
reload
	stx	$400e	; start one-shot
check
	bit	$4001
mod
	bpl	reload

	lda	$4005
	bne	check

	lda	#$30	; bmi
	sta	mod
	iny
	beq	reload

	jmp	($fffc)
	.)

not1581
	rts
