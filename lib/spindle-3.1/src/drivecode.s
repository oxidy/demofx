; Spindle by lft, linusakesson.net/software/spindle/
; This code executes inside the 1541.

; Memory
;
;  000	- Zero page; contains the gcr loop at $30-$91
;  100	- Stack; used as block buffer
;  200	- Code for serial communication
;  300	- Code for fetching data from disk
;  400	- Miscellaneous code
;  500	- GCR decoding tables
;  600	- Init, then stash buffer 1
;  700	- Stash buffer 2

#define lax1c01 .byt $af, $01, $1c
#define sax .byt $87,
#define sbx0 .byt $cb, $00

SAFETY_MARGIN	= $07

ZPORG		= $60	; $66 bytes

interested	= $d8	; 21+2 bytes
ninterested	= $ef

req_track	= $f1
currtrack	= $f2
nextstatus	= $f3
safety		= $f4
chunkend	= $f5
ledmask		= $f6
temp		= $f7
chunkprefix	= $f8
chunklen	= $f9
bufptr		= $fa	; word
nstashed	= $fc
tracklength	= $fd


	; at start of zero-page, 00:

	; 13,14,15,16,17,18,19,20
	; 5,6,7,8,9,10,11,12
	; newjob,newdisk,newtrack,0,1,2,3,4

stashbufs	= $600

; tables generated in disk.c
gcrdecode	= $500
zonebits	= $544
zonebranch	= $5bd
zonesectors	= $5c4
scramble	= $51f

;---------------------- Init --------------------------------------------------

		*=$600

		.byt	12			; sector number

;ondemand_entry
		jmp	init_post_fetch		; always

		; M-E bootstrap jumps to 604

		; Load misc, fetch, and decoding table

		.(
		ldx	#5
loop
		lda	initts,x
		sta	6,x
		dex
		bpl	loop
		.)

		lda	#1	; Read misc into $400
		jsr	dosreadblock
		lda	#0	; Read fetch into $300
		jsr	dosreadblock
		lda	#2	; Read gcr table into $500
		jsr	dosreadblock

		sei

		lda	#$02
		sta	$1800		; Indicate BUSY

		; Clear all zeropage variables

		.(
		ldx	#0
		txa
loop
		sta	0,x
		inx
		bne	loop
		.)

		; Read mode, SO enabled

		lda	#$ee
		sta	$1c0c

		;ldx	#0
		stx	$1c04		; latch, low byte
		lda	#$7f		; disable interrupts
		sta	$1c0e
		sta	$1c0d
		lda	#$c0		; enable timer 1 interrupt
		sta	$1c0e
		inx
		stx	$1c0b		; timer 1 one-shot, latch port a
		stx	$1c05		; quick first timeout

		; Copy zp code into place

		.(
		ldx	#zpcode_len - 1
loop
		lda	zpcodeblock,x
		sta	ZPORG,x
		dex
		bpl	loop
		.)

		; Load the communication code using the
		; newly installed drivecode.

		lda	#$0c
		sta	ledmask
		lda	$1c00
		and	#$03
		ora	#$4c	; led and motor on, bitrate for track 18
		sta	$1c00
		lda	#19
		sta	tracklength
		ldx	#18*2
		stx	currtrack
		stx	req_track
		inc	interested+17
		inc	ninterested
		jmp	drivecode_fetch

init_post_fetch
		.(
		lda	#6
		sta	ondemand_dest+2

		bit	$1800	; Wait until the host is up (atn held)
		bpl	*-3

		; Initialisation complete.

		lda	#1*2
		sta	req_track
		lda	#<fetch_return
		sta	mod_fetchret+1

		; We have the first continuation record
		; ready in the sector buffer.

		jmp	transfer
		.)

dosreadblock
		sta	$f9
		jmp	$d586

initts
		.byt	18,11,18,2,18,3

zpcodeblock
		*=ZPORG
prof_zp
zpc_loop
		; This nop is needed for the slow bitrates (at least for 00),
		; because apparently the third byte after a bvc sync might not be
		; ready at cycle 65 after all.

		; However, with the nop, the best case time for the entire loop
		; is 128 cycles, which leaves too little slack for motor speed
		; variance at bitrate 11.

		; Thus, we modify the bne instruction at the end of the loop to
		; either include or skip the nop depending on the current
		; bitrate.

		nop

		lax1c01				; 62 63 64 65	ddddeeee
		.byt	$6b,$f0			; 66 67		arr imm, ddddd000
		clv				; 68 69
		tay				; 70 71
zpc_mod3	lda	gcrdecode		; 72 73 74 75	lsb = 000ccccc
		ora	gcrdecode+1,y		; 76 77 78 79	y = ddddd000, lsb = 00000001

		; first read in [0..25]
		; second read in [32..51]
		; third read in [64..77]
		; clv in [64..77]
		; in total, 80 cycles from bvc

		bvc	*			; 0 1

		pha				; 2 3 4		second complete byte (nybbles c, d)
zpc_entry
		lda	#$0f			; 5 6
		sax	zpc_mod5+1		; 7 8 9

		lda	$1c01			; 10 11 12 13	efffffgg
		ldx	#$03			; 14 15
		sax	zpc_mod7+1		; 16 17 18
		.byt	$4b,$fc			; 19 20		asr imm, 0efffff0
		tay				; 21 22
		ldx	#$79			; 23 24
zpc_mod5	lda	gcrdecode,x		; 25 26 27 28	lsb = 0000eeee, x = 01111001
		eor	gcrdecode+$40,y		; 29 30 31 32	y = 0efffff0, lsb = 01000000
		pha				; 33 34 35	third complete byte (nybbles e, f)

		lax1c01				; 36 37 38 39	ggghhhhh
		clv				; 40 41
		and	#$1f			; 42 43
		tay				; 44 45

		; first read in [0..25]
		; second read in [32..51]
		; clv in [32..51]
		; in total, 46 cycles from bvc

		bvc	*			; 0 1

		lda	#$e0			; 2 3
		sbx0				; 4 5
zpc_mod7	lda	gcrdecode,x		; 6 7 8 9	x = ggg00000, lsb = 000000gg
		ora	gcrdecode+$20,y		; 10 11 12 13	y = 000hhhhh, lsb = 00100000
		pha				; 14 15 16	fourth complete byte (nybbles g, h)

		; start of a new 5-byte chunk

		lda	$1c01			; 17 18 19 20	aaaaabbb
		ldx	#$f8			; 21 22
		sax	zpc_mod1+1		; 23 24 25
		and	#$07			; 26 27
		ora	#$08			; 28 29
		tay				; 30 31

		lda	$1c01			; 32 33 34 35	bbcccccd
		ldx	#$c0			; 36 37
		sax	zpc_mod2+1		; 38 39 40
		.byt	$4b,$3f			; 41 42		asr imm, 000ccccc, d -> carry
		sta	zpc_mod3+1		; 43 44 45

zpc_mod1	lda	gcrdecode		; 46 47 48 49	lsb = aaaaa000
zpc_mod2	eor	gcrdecode,y		; 50 51 52 53	lsb = bb000000, y = 00001bbb
		pha				; 54 55 56	first complete byte (nybbles a, b)

		tsx				; 57 58
BNE_WITH_NOP	=	(zpc_loop - (* + 2)) & $ff
BNE_WITHOUT_NOP	=	(zpc_loop + 1 - (* + 2)) & $ff
zpc_bne		.byt	$d0,BNE_WITHOUT_NOP	; 59 60 61	bne zpc_loop

		ldx	zpc_mod3+1		; 61 62 63
		lda	$1c01			; 64 65 66 67	ddddeeee
		jmp	zp_return

zpcode_len	=	* - ZPORG

		.dsb	$700 - zpcodeblock - zpcode_len, $aa

;---------------------- Miscellaneous code ------------------------------------

		*=$400

prof_misc

ondemand_fetchret

		; The new drivecode is now on the stack page; move it.

		.(
		ldx	#0
loop
		lda	$100,x
+ondemand_dest
		sta	$200,x
		inx
		bne	loop

		jmp	ondemand_entry
		.)

nothing_fetched
		.(
		; More sectors in the current batch?

		lda	ninterested
		bne	fetchmore

		; Nothing new to fetch.
		; Is the next batch on a new track?

		.(
		lda	#$40
		bit	2
		beq	nonewtrack

		eor	2
		sta	2

		ldx	req_track
		inx
		inx
		cpx	#18*2
		bne	not18

		ldx	#19*2
not18
		stx	req_track
		bne	fetchmore
nonewtrack
		.)

		; Do we have a stashed sector?

		ldx	nstashed
		beq	nostash
+sendstash
		dex
		stx	nstashed
		txa
		ora	#>stashbufs
		jmp	transferbuf
nostash
		.)

		; Unpack the continuation buffer.

		.(
		ldy	#0
		ldx	#20
		sec
newbyte
		lda	0,y
		iny
bitloop
		ror
		bcc	notset

		beq	newbyte

		inc	interested,x
		inc	ninterested
		clc
notset
		dex
		bpl	bitloop

		lsr
		bcc	nodemand

		lda	#<ondemand_fetchret
		sta	mod_fetchret+1
		lda	#18*2
		sta	req_track
nodemand
		lda	#0
		sta	bufptr+1
		sta	mod_buf+2

		; y = $03 where the chain heads are stored

		beq	checkunit0	; always
		.)

fetch_return
		; We have a valid sector in the buffer.
		; x is the sector number and z is set if
		; ninterested was decremented to zero.

		.(
		beq	dontstash

		ldy	nstashed
		cpy	#2
		bcs	dontstash

		lda	interested+2,x
		bne	dostash

		lda	#$04
		and	$1800
		bne	dontstash	; the host is ready right now

dostash
		; Stash this sector, then go fetch another one.

		tya
		ora	#>stashbufs
		sta	mod_dest+2

		ldx	#0
loop
		lda	$100,x
mod_dest
		sta	stashbufs,x
		dex
		bne	loop

		inc	nstashed
+fetchmore
		jmp	drivecode_fetch
dontstash
		.)

transfer
		; Turn off LED.

		lda	$1c00
		and	#$77
		sta	$1c00

		lda	#1
transferbuf
		sta	bufptr+1
		sta	mod_buf+2

		; Full block?

		.(
		ldy	#0
		lda	(bufptr),y
		bpl	notfull

		lda	#$ff
		bmi	nextunit		; always
notfull
		.)

		; Is there a continuation record?

		.(
		iny
		asl
		bpl	nocontrec

		ldx	#0

		; A continuation record begins with a 3-byte
		; sector set that can be followed by one or more
		; postponed units of size 2..4.

		lda	#2	; number of bytes to copy - 1
headloop
		sta	temp
copy
		lda	(bufptr),y
		iny
		sta	0,x
		inx
		dec	temp
		bpl	copy

		lda	(bufptr),y
		beq	headdone

		cmp	#5
		bcc	headloop
headdone
		cpx	#3
		bne	neednodummy

		; No chain heads. We have to insert a do-nothing unit
		; in order to report to the host whether the job is complete.

		txa
		sta	0,x
		ldx	#7
neednodummy
		lda	#0
		sta	0,x
nocontrec
		.)

		; Y points to the length byte of the first hostside unit.

checkunit0
		jmp	checkunit

nextunit
		; A is the length of the next unit.

		; We use the length to compute where the unit ends.
		; We will also transfer the length, but for that we
		; have to scramble the bits.

		sta	chunklen	; not counting the length byte

		ldx	#$09
		.byt	$cb,0		; sbx imm
		eor	scramble,x
		sta	chunkprefix
		jmp	comm_continue

async_cmd
		.(
		; Pull data to indicate busy.
		; 33 cycles after atn edge, worst case.

		;lda	#2
		sta	$1800

		; Incoming asynchronous command. Clear pending work.

		ldx	#23
		lda	#0
loop
		sta	interested,x	; also ninterested
		dex
		bpl	loop

		sta	nstashed

		; Fetch command-handling code and jump to it.

		inc	interested+6
		inc	ninterested
		lda	#<ondemand_fetchret
		sta	mod_fetchret+1
		lda	#>ondemand_fetchret
		sta	mod_fetchret+2
		lda	#18*2
		sta	req_track
		bne	fetchmore	; always
		.)

		.dsb	$500 - *, $bb

;---------------------- Fetch -------------------------------------------------

		* = $300

prof_track
drivecode_fetch
		.(
wait		bit	$1c0d		; wait for previous step to settle
		bpl	wait
		.)

		.(
		lda	$1c00

		ldx	req_track
		cpx	currtrack
		beq	fetch_here

		and	#$0b		; clear zone and motor bits for now
		bcs	seek_up
seek_down
		dec	currtrack
		;clc
		adc	#$03		; bits should decrease
		bcc	do_seek		; always
seek_up
		inc	currtrack
		;sec
		adc	#$01-1		; bits should increase
do_seek
		ldy	#3
		cpx	#31*2
		bcs	ratedone
		dey
		cpx	#25*2
		bcs	ratedone
		dey
		cpx	#18*2
		bcs	ratedone
		dey
ratedone
		ldx	zonebranch,y
		stx	zpc_bne+1

		ldx	zonesectors,y
		stx	tracklength

		ora	zonebits,y	; also turn on motor and LED
		sta	$1c00

		ldy	#$19
		sty	$1c05		; write latch & counter, clear int
nosectors
		jmp	nothing_fetched

fetch_here
		ldx	ninterested
		beq	nosectors

		ora	ledmask		; turn on motor and usually LED
		sta	$1c00
		.)

fetchblock
		ldx	#$f0		; beq
		lda	safety
		beq	nosaf

		ldx	#$a9		; lda imm
nosaf
		stx	mod_safety

prof_sync
		; Wait for a data block

		ldx	#0		; will be ff when entering the loop
		txs
waitsync
		bit	$1c00
		bpl	*-3

		bit	$1c00
		bmi	*-3

		lda	$1c01	; ack the sync byte
		clv
		bvc	*
		lda	$1c01	; aaaaabbb, which is 01010.010(01) for a header
		clv		; or 01010.101(11) for data
		eor	#$55
		bne	waitsync

		bvc	*
		lda	$1c01	; bbcccccd
		clv
		.byt	$4b,$3f			; asr imm, d -> carry
		sta	first_mod3+1

		bvc	*
		lax1c01				; 0 1 2 3	ddddeeee
		.byt	$6b,$f0			; 4 5		arr imm, ddddd000
		clv				; 6 7
		tay				; 8 9
first_mod3	lda	gcrdecode		; 10 11 12 13	lsb = 000ccccc
		ora	gcrdecode+1,y		; 14 15 16 17	y = ddddd000, lsb = 00000001
		pha				; 18 19 20	first byte to $100

		; get sector number from the lowest 5 bits of the first byte

		and	#$1f			; 21 22
		tay				; 23 24
		lda	interested,y		; 25 26 27 28
mod_safety
		beq	notint			; 29 30

		jmp	zpc_entry		; 31 32 33	x = ----eeee
zp_return
		.byt	$6b,$f0			; arr imm, ddddd000
		tay
		lda	gcrdecode,x		; x = 000ccccc
		ora	gcrdecode+1,y		; y = ddddd000, lsb = 00000001

prof_sum

#if GENERATE_ERRORS
		.(
		ldx	#$3f
loop
		eor	$100,x
		eor	$140,x
		eor	$180,x
		eor	$1c0,x
		dex
		bpl	loop
		.)

		tax
		bne	badsum

		lda	$1804			; timer low-byte
		cmp	err_prob
		bcc	badsum
#else
		.(
		ldx	#$1f
loop
		eor	$100,x
		eor	$120,x
		eor	$140,x
		eor	$160,x
		eor	$180,x
		eor	$1a0,x
		eor	$1c0,x
		eor	$1e0,x
		dex
		bpl	loop
		.)

		tax
		bne	badsum
#endif

		lsr	safety
		bcs	badsum

		lda	$100
		and	#$1f
		tax
		lsr	interested,x
		dec	ninterested

mod_fetchret	jmp	ondemand_fetchret	; usually jmp fetch_return

notint
		.(
		; Are we keeping up with the Commodore?

		; The block under the drive head isn't interesting.
		; So if no interesting block is about to arrive...

		ldx	#2
loop
		iny
		cpy	tracklength
		bcc	nowrap

		ldy	#0
nowrap
		lda	interested,y
		bne	fetchblock0

		dex
		bne	loop

		; ...and we have a stashed sector...

		ldx	nstashed
		beq	fetchblock0

		; ...and the host has nothing better to do...

		lda	#$04
		bit	$1800
		beq	fetchblock0

		; ...then now is a good time to transmit it.

		jmp	sendstash
fetchblock0
		.)

badsum
		jmp	fetchblock

		.dsb	$400 - *, $cc

;---------------------- Communicate -------------------------------------------

		*=$200

		.byt	17|$40			; sector number + cont rec

		; These are modified when the disk image is created.

		.byt	0,0,0		; Initial sector set
		.byt	0		; No regular units
sideid
		; Knock code for this disk side.
		; Putting the branch offsets here is a kludge to bring them
		; to disk.c so they can be included in the gcr table.

		.byt	0,BNE_WITH_NOP,BNE_WITHOUT_NOP
err_prob
		.byt	0		; Error probability

prof_comm

nodatarequest
		beq	reset

		jmp	async_cmd

reset
		; Atn was released but no other line is held.

		jmp	($fffc)		; System reset detected -- reset drive.

comm_continue
		sty	temp
		tya
		clc
		adc	chunklen
		tay
		sty	chunkend

		ldx	#$02		; MORE

		lda	bufptr+1	; are we transferring postponed units?
		bne	notlast

		; Wait for the host to pull clock before we
		; can transmit a postponed unit.

		lda	#$04
waitclk
		bit	$1800
		beq	waitclk

		cpx	chunklen	; is it a chain head?
		bne	nochain

		ldx	#$a		; MORE + force chain
nochain
		; y points to last byte of unit

		lda	1,y		; more postponed units to follow?
		bne	notlast

		bit	2		; check newjob flag of next batch
		bpl	notlast

		dex			; turn off MORE, keep force-chain bit
		dex
notlast
		lda	nextstatus	; bit 1 set if the old job continues
		stx	nextstatus

		and	#$02
		bne	keepmotor

		; We are about to wait for the host at a job boundary.
		; If the host doesn't pull clock within one second,
		; we'll turn off the motor (and LED).

		tax	; x = 0
outermotor
		lda	#$9e
		sta	$1805
		lda	#$04
innermotor
		bit	$1800
		bne	keepmotor

		bit	$1805
		bmi	innermotor

		inx
		bpl	outermotor

		lda	$1c00
		and	#$f3
		sta	$1c00
		lda	#SAFETY_MARGIN
		sta	safety

keepmotor
		; Was atn released prematurely? Then reset.

		lda	$1800
		bpl	reset

		lda	#$10
		sta	$1800		; release BUSY (data)

		bit	$1800
		bmi	*-3		; wait for atn to be released
prof_send
		ldx	#0
		stx	$1800		; prepare to read data/clock lines
		ldy	temp

		lda	$1800
		.byt	$4b,5		; asr imm
		bcc	nodatarequest

		bne	readyforchain	; host did pull clock

		; The host is currently dealing with a chain, so we
		; have to pull clock as part of the exit status flags
		; (it gets inverted in transmission).

		lda	nextstatus
		ora	#$08
		sta	nextstatus

		; In this case, we know the motor is already running.

		bne	motorok		; always
readyforchain

		; This could be the first transfer of a new job.
		; Warm up the motor for the next block.

		lda	$1c00
		ora	#$04
		sta	$1c00
motorok

		; Send chunkprefix, then from y+1 to chunkend inclusive, then nextstatus.

		; First bit pair is on the bus 51 cycles after atn edge, worst case.

		.byt	$a7,chunkprefix	; lax zp
		and	#$0f
		bpl	sendentry	; always

		; Writes to $1800 must happen 13 cycles after atn changed, worst case.
		; Atn can change 4 cycles after writing to $1800, worst case.
		; This allows up to 7 cycles between each check+write idiom.
sendloop
		.byt	$4b,$f0		; asr imm

		bit	$1800
		bmi	*-3
		sta	$1800		; 0--cd000

		lsr
		.byt	$4b,$f0		; asr imm
		iny

		bit	$1800
		bpl	*-3
		sta	$1800		; 000ab000 (a gets inverted due to atna)
mod_buf
		.byt	$bf,$00,$01	; abcdefgh, lax $100,y
		and	#$0f

		bit	$1800
		bmi	*-3
sendentry
		sta	$1800		; 0000e-g-

		asl
		ora	#$10
		cpy	chunkend

		bit	$1800
		bpl	*-3
		sta	$1800		; 0001f-h0

		txa
		bcc	sendloop

		.byt	$4b,$f0		; asr imm

		bit	$1800
		bmi	*-3
		sta	$1800		; 0--cd000 (final byte)

		lsr
		.byt	$4b,$f0		; asr imm

		bit	$1800
		bpl	*-3
		sta	$1800		; 000ab000 (final byte, a gets inverted)

		lda	nextstatus

		bit	$1800
		bmi	*-3
		sta	$1800		; send status bits

		lda	#$2

		bit	$1800
		bpl	*-3
		sta	$1800		; pull data (BUSY), release the clock line

		; done with unit

		iny
		beq	nomoreunits

checkunit
		lda	(bufptr),y
		beq	nomoreunits

		jmp	nextunit
nomoreunits
		jmp	drivecode_fetch

		.dsb	$300 - *, $dd

;---------------------- Flip disk / end ---------------------------------------

		*=$600

		.byt	5			; sector number

ondemand_entry
		.(

		; We have transmitted the chain heads for the last job on the
		; old disk, and the host has returned from the last regular
		; loadercall. Then we've stepped to track 18 and retrieved
		; this code and jumped to it.

		lda	#<flip_fetchret
		sta	mod_fetchret+1
		lda	#>flip_fetchret
		sta	mod_fetchret+2
		lda	#$04
		sta	ledmask

badflip
		; Replace the buffer with instructions to transfer a do-nothing
		; unit and then read sector 17 (with the initial continuation
		; record).

		; If the host isn't ready, we'll turn off the motor and wait
		; for the dummy transfer.

		; When the extra "flip disk" loadercall happens, we proceed to
		; alternate between transferring do-nothing units and reading
		; sector 17 again, until the knock codes match or the host
		; intervenes with a command or system reset.

		ldy	#10
copyretry
		lda	retrysector,y
		sta	$100,y
		dey
		bpl	copyretry

		jmp	transfer

flip_fetchret
		; Do the knock codes match?

		ldx	#2
flipcheck
		lda	$105,x
		cmp	nextsideid,x
		bne	badflip

		dex
		bpl	flipcheck

		; Yes, this is the new diskside.

		; Set the new-job flag for the initial continuation record.
		; This makes the "flip disk" loadercall return.

		asl	$103
		sec
		ror	$103

		lda	#$0c
		sta	ledmask
		lda	#1*2
		sta	req_track
		lda	#<fetch_return
		sta	mod_fetchret+1
		lda	#>fetch_return
		sta	mod_fetchret+2

		; We have a continuation record ready in the sector buffer.

		jmp	transfer

		.dsb	$700 - 14 - *, $ee

retrysector
		.byt	$40		; Continuation record indicator
		.byt	8,0,0		; Continue with sector 17
		.byt	5,0,$bf,0,$8f,0	; Dummy data unit (patched in disk.c)
		.byt	0		; No more units

nextsideid
		.byt	0,0,0		; Patched
		.)

;---------------------- Asynchronous command ----------------------------------

		*=$600

		.byt	6			; sector number

;ondemand_entry
		.(
		ldy	#0
		lda	#2
		sta	temp
		asl				; a = 4 for bit-check
		;clc
bitloop
		bit	$1800
		bpl	async_reset		; system reset detected

		ldx	#$10
		stx	$1800			; release data

		bit	$1800			; wait for atn to be released
		bmi	*-3

		sty	$1800			; get ready to read
		bit	0

		ldx	#$02

		bit	$1800
		beq	got0

		sec
got0
		stx	$1800			; pull data

		bit	$1800			; wait for atn to be pulled
		bpl	*-3

		rol	temp
		bcc	bitloop

		; At this point, the host returns from the seek call.

		ldy	temp
		lda	seektrack,y
		sta	req_track
		ldx	seeksector,y
		inc	interested,x
		inc	ninterested

		lda	#$0c
		sta	ledmask
		lda	#<seek_fetchret
		sta	mod_fetchret+1
		lda	#>seek_fetchret
		sta	mod_fetchret+2
		jmp	drivecode_fetch

async_reset
		jmp	($fffc)

seek_fetchret
		; We have the desired continuation record on the stack.
		; Cut off any further units in the same sector.

		lda	#$40
		sta	$100			; no 255-byte unit, cont rec
		lda	#0
		sta	$104			; no further units

		; Clear the new-job flag (msb of $103).

		; Also, if it was already clear, this label was
		; targetting the first job. In that case, we should
		; change from track 18 to track 1.

		asl	$103
		bcs	notfirst

		lda	#1*2
		sta	req_track
notfirst
		lsr	$103

		lda	#<fetch_return
		sta	mod_fetchret+1
		lda	#>fetch_return
		sta	mod_fetchret+2
		jmp	transfer

		; When we transfer, the continuation record is copied
		; into the continuation buffer along with a do-nothing
		; unit. Then, because there are no more interesting sectors,
		; the continuation buffer is unpacked and the do-nothing
		; unit is transferred.
		; Then we will go and prefetch some interesting sectors.

		.dsb	$680 - *, $ff

seektrack	.dsb	64,0
seeksector	.dsb	64,0
		.)

prof_end	= $800
