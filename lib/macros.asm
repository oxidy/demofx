// Emit `count` NOP instructions (2 CPU cycles each).
.macro nops(count) {
    .for (var i = 0; i < count; i++) {
        nop
    }
}

// Alias for nops — DemoFX codegen uses nop(N).
.macro nop(n) {
    :nops(n)
}

// Wait approximately `count` CPU cycles (not nop count).
// Odd counts: bit $ea (3 cycles) + nops for the remainder.
.macro cycles(count) {
    .if (count < 0) {
        .error "The cycle count cannot be less than 0 (" + count + " given)."
    }
    .if (count == 1) {
        .error "Can't wait only one cycle."
    }
    .if (mod(count, 2) != 0) {
        bit $ea
        .eval count -= 3
    }
    :nops(count / 2)
}

.macro push_stack() {
    pha
    txa
    pha
    tya
    pha
    lda $01
    pha
}
       
.macro pull_stack() {
    pla
    sta $01
    pla
    tay
    pla
    tax
    pla
}

// Slim A/X/Y save for double-IRQ stabilize — do NOT save $01 here (too slow
// before inc $d012). Caller sets $01=$35, then stabilize; leave $01 at $35 on exit.
.macro push_stack_stable() {
    pha
    txa
    pha
    tya
    pha
}

.macro pull_stack_stable() {
    pla
    tay
    pla
    tax
    pla
}

// Wait until raster crosses into the lower half (bit7 of $d011 set), then
// until the next frame starts (bit7 clear) — cheap vertical sync / frame boundary.
.macro d011_WaitForVerticalSync() {
    bit $d011
    bpl *-3
    bit $d011
    bmi *-3
}

.macro irq_call_wait(subroutine, rasterline) {
    irq_setup(next,rasterline)
    inc $d019 
    cli 
    running: 		// - safe so we wont execute while still running
        lda #0
        bne dont_call
        inc running+1
            jsr subroutine
        lda #0
        sta running+1 
    dont_call:
    irq_exit()
next:
    irq_enter()
}

.macro irq_wait(rasterline) {
        irq_setup(next,rasterline)
        irq_exit()
    next:
        irq_enter()
    }

.macro irq_init(rasterline,background_loop) {
        irq_setup(next,rasterline)
        cli 
        jmp background_loop
    next:
        irq_enter()
    }

.macro irq_setup(addr, rasterline) {
    // Local labels — access via KickAss namespace on the call site:
    //   irq_1_next: irq_setup(irq_2, $30)
    // then patch: sta irq_1_next.fffe+1 / .ffff+1 / .nextd012+1
    .label fffe = *
    lda #<addr
    sta $fffe
    .label ffff = *
    lda #>addr
    sta $ffff
    .label nextd012 = *
    lda #rasterline
    sta $d012
}

.macro irq_exit() {
    inc $d019  		// Acknowledge interupt
    pull_stack()    	// Load A,X,Y,CPU to stack
    rti  			// Return from interupt
}

.macro irq_enter() {
    push_stack() 		// Save A,X,Y,CPU to stack
}
