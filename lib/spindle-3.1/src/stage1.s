; Spindle by lft, linusakesson.net/software/spindle/
; This is the small bootstrap prg loaded by the kernal,
; containing the resident part of the loader and decruncher.

#if RELOC

zp_last_lsb	= $55
zp_dest		= $55
zp_link		= $55

loaderorg	= $5500
buffer		= $5500

entrypoint	= $5555

#else

zp_last_lsb	= $f4
zp_dest		= $f5
zp_link		= $f7

loaderorg	= $200
buffer		= $700

entrypoint	= $801

#endif

zp_msb		= zp_last_lsb

zp_s1_unit	= $fb

	.word	basicstub
	*=$801
basicstub
	.byt	$0b,$08,$01,$00,$9e,"2061",0,0,0

	; What unit should we load from?

	.(
	ldx	#8

	lda	$ba
	bne	nodef
	txa
nodef
	sta	zp_s1_unit
	.)

	; Silence other drives

	.(
sil_loop
	cpx	zp_s1_unit
	beq	sil_next

	stx	$ba
	lda	#0
	sta	$90
	txa
	jsr	$ffb1	; listen
	lda	#$ff	; open 15
	jsr	$ff93	; second
	lda	$90
	pha
	jsr	$ffae	; unlstn
	pla
	bmi	nodrive

	; Something responds to this iec address

	ldx	#0
chunk_loop
	jsr	upload_chunk
	.byt	$af		; lax abs
	.word	cmd_upload+3
	.byt	$cb,$e0		; sbx imm
	cpx	#$a0
	bcc	chunk_loop

	ldx	#<cmd_runsil
	ldy	#>cmd_runsil
	lda	#5
	jsr	send_command
nodrive
	ldx	$ba
sil_next
	inx
	cpx	#12
	bcc	sil_loop
	.)

	; Launch the drivecode

	.(
	ldx	zp_s1_unit
	stx	$ba

	ldx	#<cmd_install
	ldy	#>cmd_install
	lda	#size_install
	jsr	$ffbd	; setnam

	lda	#15
	tay
	ldx	$ba
	jsr	$ffba	; setlfs

	jsr	$ffc0	; open
	.)

	; Turn off CIA interrupt

	lda	#$7f
	sta	$dc0d

	lda	#$35
	sei
	sta	1

	; Release the lines

	lda	#$3c
	sta	$dd02
	ldx	#0
	stx	$dd00

	; Get TV standard flag

	ldy	$2a6

	; While the drive is busy fetching drivecode, move the
	; loader into place

	.(
loop
	lda	loadersrc,x
	sta	loaderorg,x
	inx
	bne	loop
	.)

	.(
	; Slow down serial transfer on NTSC.

	tya
	beq	slow

	; Slow down serial transfer if other drives are present.

	lda	cmd_upload+3
	beq	fast
slow
	ldy	#3
loop
	ldx	patchoffset,y
	lda	loaderorg,x
	ora	#$10		; abs -> abs,x
	sta	loaderorg,x
	lda	#$f8
	sta	loaderorg+1,x
	dec	loaderorg+2,x
	dey
	bpl	loop
fast
	.)

	; Wait for drive to signal BUSY.

	bit	$dd00
	bmi	*-3

	; Pull ATN.

	lda	#$08
	sta	$dd00

	; The first loadset may overwrite stage1, so
	; we have to fake a jsr.

	lda	#>entrypoint
	pha
	lda	#<entrypoint
	pha

	; Make the first loader call

	jmp	loaderorg

upload_chunk
	.(
	stx	cmd_upload+3	; target addr
	ldy	#0
loop
	lda	silencesrc,x
	sta	cmd_upload+6,y
	inx
	iny
	cpy	#32
	bcc	loop
	.)

	ldx	#<cmd_upload
	ldy	#>cmd_upload
	lda	#6+32

send_command
	jsr	$ffbd		; setnam
	ldy	#15
	ldx	$ba
	tya
	jsr	$ffba		; setlfs
	jsr	$ffc0		; open
	lda	#15
	jmp	$ffc3		; close

; ---------------------------------------------------

silencesrc
	.bin	0,0,"silence.bin"

cmd_runsil
	.byt	"M-E",$03,$04

cmd_install
	.(
	; 23 bytes out of 42

	.byt	"M-E"
	.word	$205

	; Load first drivecode block into buffer 3 at $600
	lda	#18
	sta	$c
	lda	#12
	sta	$d
	lda	#3
	sta	$f9
	jsr	$d586

	jmp	$604
	.)
size_install	= * - cmd_install

; ---------------------------------------------------

patchoffset
	.byt	<speedpatch1	; eor $dd00
	.byt	<speedpatch2	; ora $dd00
	.byt	<speedpatch3	; and $dd00
	.byt	<speedpatch4	; lda $dd00

; ---------------------------------------------------

loadersrc
	* = loaderorg

prof_wait

	; dd02 required to be 001111xx at this point, and we're pulling atn (but not clock or data)

	; status flags 00dc1000
	; d = pull data = no more data expected
	; c = pull clock = not working on a chain
	; drive pulls data if no data is available right now

	lda	#$18		; want data, no ongoing chain

mainloop
patch_1
	.byt	$80,1		; nop imm, becomes inc 1 in shadow RAM mode

	cmp	#$38		; nothing more to do?
	beq	jobdone

	;clc
	sta	$dd00		; send status

checkbusy
	bit	$dd00		; check status
	bmi	transfer	; more data is expected AND available

	bvc	checkbusy	; if we are pulling clock, we have no chain to work on

; ---------------------------------------------------

	; preserves data and atn, may set clock bit

prof_link

patch_2
	.byt	$80,1		; nop imm, becomes dec 1 in shadow RAM mode
	pha			; save status flags

linkloop
	;clc
	ldy	#0

	.byt	$b3,zp_link	; lax (zp),y to get next pointer lsb into x
	iny
	lda	(zp_link),y	; get the command byte

	; 10nnnnoo oooooooo	long copy (length n+3, offset o+1)
	; 0ooooooo		short copy (length 2, offset o)

	bpl	linkshort

	pha
	lsr
	.byt	$4b,$fe		; asr imm, 0010nnnn
	adc	#$e0+2
	tay
	lda	(zp_link),y

	; y = number of bytes to copy - 1
	; stack, a = offset - 1

	;sec
	adc	zp_link
	sta	mod_src+1
	pla
	and	#3

copycommon
	adc	zp_link+1
	sta	mod_src+2

copyloop
mod_src
	lda	!0,y
	sta	(zp_link),y
	dey
	bpl	copyloop

	cpx	zp_link
	stx	zp_link
	bcc	linkloop

	bne	linknotdone

	pla			; either 08 or 28
	ora	#$10
	.byt	$f0		; beq, skip pla

linknotdone
	pla

	; a is either 08, 18, 28, or 38

	dec	zp_link+1
	bcs	mainloop	; always

linkshort

	; y = number of bytes to copy - 1
	; a = offset

	;clc
	adc	zp_link
	sta	mod_src+1
	lda	#0
	beq	copycommon	; always

jobdone
	rts

; ---------------------------------------------------

transfer
prof_xfer

	; a is either 08 or 18

	eor	#$28		; becomes either 20 or 30
	sta	$dd00		; release atn while pulling data to request first bit pair

	; data must remain held for 22 cycles
	; and the first bit pair is available after 51 cycles

	ldy	#7
delay
	dey
	bpl	delay

	ldx	#8
	.byt	$8f,$00,$dd	; sax abs, release data so we can read it
	bcc	receive		; always

recvloop
	ora	0		; aa101111
	lsr
	lsr
speedpatch1
	eor	$dd00		; bbaa00xx
	.byt	$8f,$00,$dd	; sax abs, release atn to request third pair
	lsr
	lsr
	iny
	sty	mod_store+1
speedpatch2
	ora	$dd00		; ccbbaaxx
	stx	$dd00		; pull atn to request fourth pair
	lsr
	lsr
	sta	mod_last+1
	lda	#$c0
speedpatch3
	and	$dd00
	sta	$dd00		; release atn to request first pair (or status)
mod_last
	ora	#0		; ddccbbaa
mod_store
	sta	buffer
	cpy	buffer		; carry set if this was the last byte
receive
speedpatch4
	lda	$dd00		; aa0000xx
	stx	$dd00		; pull atn to request second pair (or ack status)
	bcc	recvloop

	pha			; save status bits (more to receive & chain flag)

	; buffer contains crunched data

	; 0		N = index of last byte of buffer
	; 1..N-3	crunched byte stream, backwards
	; N-2		last link pointer
	; N-1		target base address lsb
	; N		target base address msb

	; or the head of a link chain

	; 0		2
	; 1		first link pointer lsb
	; 2		first link pointer msb

	; or an entry vector (pefchain uses this for seeking and disk changes)

	; 0		3
	; 1		4c (jmp)
	; 2		lsb
	; 3		msb

patch_3
	.byt	$80,1		; nop imm, becomes dec 1 in shadow RAM mode

	tya
	tax
	lda	buffer,x
	sta	zp_dest+1

	dex
	ldy	buffer,x
	sty	zp_dest

	dex
	bne	expand

	; in this case, the drive will also have set
	; the chain flag for us

	sty	zp_link
	sta	zp_link+1

unitdone
	pla			; status bits, DC0000xx
	lsr
	lsr
	ora	#$08
	jmp	mainloop

; ---------------------------------------------------

expand
prof_expand

	lda	buffer,x
	sta	zp_last_lsb

	; 11nnnnnn		literal (length n+1)
	; 10nnnnoo oooooooo	long copy (length n+3, offset o+1)
	; 0ooooooo		short copy (length 2, offset o)

clc_nextitem
	clc

nextitem
	dex
	beq	unitdone

	ldy	#0
	lda	buffer,x
	adc	#$40
	bcs	literal

gotcopy
	lda	zp_last_lsb
	sta	(zp_dest),y
	lda	zp_dest
	sta	zp_last_lsb
	iny
	lda	buffer,x
	sta	(zp_dest),y
	bmi	longcopy

shortcopy
	lda	#2
	bcc	advance		; always

longcopy
	lsr			; 010nnnno
	.byt	$4b,$fe		; asr imm, 0010nnnn
	adc	#$e0+2
	tay

literal
	sta	mod_end+1

	.byt	$a9		; lda imm, skip first iny
litloop
	iny
	dex
	lda	buffer,x
	sta	(zp_dest),y
mod_end
	cpy	#0
	bcc	litloop

	;sec
	tya
advance
	adc	zp_dest
	sta	zp_dest
	bcc	nextitem

	inc	zp_dest+1
	bcs	clc_nextitem	; always

prof_end
	.dsb	loaderorg+$100-*,0

	* = loadersrc+$100

; ---------------------------------------------------

cmd_upload
	.byt	"M-W",$00,$04,$20
