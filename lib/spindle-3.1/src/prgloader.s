; Spindle by lft, linusakesson.net/software/spindle/
; Simple loader and driver for pef2prg.

src	=	$10
count	=	$12
dest	=	$14

	* = $801 - 2
	.word	$801
	.word	$80b,1
	.byt	$9e,"2061",0
	.word	0

start
	lda	#$7f
	sta	$dc0d

	sei
	lda	#$35
	sta	1

	lda	#$00
	sta	$dd00
	lda	#$3c
	sta	$dd02

	jsr	earlysetup

	.(
	ldx	#0
loop
	lda	driversrc,x
	sta	$200,x
	inx
	bne	loop
	.)

	lda	stream_pointer
	sta	src
	lda	stream_pointer+1
	sta	src+1

	dec	1

	jmp	drv_install


driversrc
	* = $200

drv_play
	.(
	; player_time * 63 cycles
	; including jsr+rts and border effect

	dec	$d020
	ldx	player_time
	dex
	beq	skip
loop
	jsr	drv_rts
	jsr	drv_rts
	jsr	drv_rts
	jsr	drv_rts
	nop
	nop
	nop
	nop
	nop
	dex
	bne	loop

	nop
skip
	jsr	drv_rts
	jsr	drv_rts
	nop
	nop
	nop

	inc	$d020
	;rts
	.)

drv_rts
	rts

drv_install
	.(
nextchunk
	lda	src
	sec
	sbc	#4
	sta	src
	lda	src+1
	sbc	#0
	sta	src+1
	cmp	#$0a
	bcc	drv_run

	ldy	#3
header
	lda	(src),y
	sta	count,y
	dey
	bpl	header

	; copy count bytes from --src to --dest

	ldy	count
	beq	aligned

	lda	src
	sec
	sbc	count
	sta	src
	bcs	noc2

	dec	src+1
noc2
	lda	dest
	sec
	sbc	count
	sta	dest
	bcs	noc3

	dec	dest+1
noc3

msbloop
	dey
	beq	lsbdone

lsbloop
	lda	(src),y
	sta	(dest),y
	dey
	bne	lsbloop

lsbdone
	lda	(src),y
	sta	(dest),y

aligned
	lda	count+1
	beq	nextchunk

	dec	src+1
	dec	dest+1
	dec	count+1
	jmp	msbloop
	.)

drv_run
	.(
	inc	1
	inc	$2ff	; Enable vice monitor checking

	jsr	v_prepare
	jsr	v_setup

	lda	v_irq
	sta	$fffe
	lda	v_irq+1
	sta	$ffff

	ora	$fffe
	beq	mainloop

	lsr	$d019
	cli

mainloop
	jsr	v_main

	lda	#$ff
	sta	$dc02
	lsr
	sta	$dc00
	lda	#$10
	bit	$dc01
	bne	mainloop

fadeloop
	jsr	v_main
	jsr	v_fadeout
	bcc	fadeloop

	jsr	v_cleanup
	jmp	*
	.)

	.dsb	$300-19-*,0

	; The following fields are modified by pef2prg

v_prepare
	jmp	drv_rts
v_setup
	jmp	drv_rts
v_main
	jmp	drv_rts
v_fadeout
	jmp	drv_rts
v_cleanup
	jmp	drv_rts
v_irq
	.word	0
player_time
	.byt	25

	.byt	$a9	; Used by monitor commands

	* = driversrc + $100

stream_pointer
	.word	0

earlysetup
	; Early setup code is added by pef2prg.
	; Up to 128 bytes, mustn't cross a page boundary.
