; Spindle by lft, www.linusakesson.net/software/spindle/

; The code in this file (template/effect.s) is in the public domain.

		; efo header

		.byt	"EFO2"		; fileformat magic
		.word	0		; prepare routine
		.word	setup		; setup routine
		.word	interrupt	; irq handler
		.word	0		; main routine
		.word	0		; fadeout routine
		.word	0		; cleanup routine
		.word	call_play	; location of playroutine call

		; tags go here

		;.byt	"P",$04,$07	; range of pages in use
		;.byt	"I",$10,$1f	; range of pages inherited
		;.byt	"Z",$02,$03	; range of zero-page addresses in use

		.byt	"S"		; interrupt is i/o safe
		;.byt	"U"		; main/prepare/fadeout are i/o unsafe
		;.byt	"X"		; no loading during part
		;.byt	"A"		; avoid loading (as much as possible)
		;.byt	"M",<play,>play	; install music playroutine
		;.byt	"J"		; fadeout returns with a jump target

		.byt	0		; end of tags

		.word	loadaddr
		* = $3000
loadaddr

setup
		lda	#$3d
		sta	$dd02
		lda	#0
		sta	$d015
		sta	$d017
		sta	$d01b
		sta	$d01c
		sta	$d01d
		lda	#$08
		sta	$d016
		lda	#$1b
		sta	$d011
		lda	#$30
		sta	$d012
		rts

interrupt
		dec	0		; 10..18, enable I/O
		sta	int_savea+1	; 15..23

		; Jitter correction. Put earliest cycle in parenthesis.
		; (10 with no sprites, 19 with all sprites, ...)
		; Length of clockslide can be increased if more jitter
		; is expected, e.g. due to NMIs.

		lda	#39-(10)	; 19..27 <- (earliest cycle)
		sec			; 21..29
		sbc	$dc06		; 23..31, A becomes 0..8
		sta	*+4		; 27..35
		bpl	*+2		; 31..39
		lda	#$a9		; 34
		lda	#$a9		; 36
		lda	#$a9		; 38
		lda	$eaa5		; 40

		; at cycle 34+(10) = 44

		stx	int_savex+1
		sty	int_savey+1

call_play	bit	!0		; gets replaced by jsr to playroutine

		; effect goes here

int_savea	lda	#0
int_savex	ldx	#0
int_savey	ldy	#0
		lsr	$d019
		inc	0
		rti
