[Back to Main](../README.md)  |  [Back to Usage](usage.md)  


## IRQ

IRQ's are created as objects in Java, and these will be compiled 
into normal asm-code for you, with timing and everything. You only need
to understand a few parameters and you will save yourself a lot of time.

## How to create IRQ's

#### Single IRQ

`IRQ irq1 = new IRQ(1, 1, Timer.enabled, Stabilize.on, "30", "30");`

Let's go through the parameters.

| Name         | Explanation  |  
|--------------|--------------|  
| thisIRQIndex | The index for this IRQ |  
| nextIRQindex | The index for the next IRQ |  
| timerTickEnabled | Increment timer tick |  
| stabilizeThisIRQ | Stabilize IRQ |  
| thisIRQ $d012 | $d012 entry point for this IRQ |  
| nextIRQ sd012 | Sd012 entry point for next IRQ |  

Let's explain that in detail;  

You want a single IRQ for something simple, perhaps for showing a bitmap image and playing a SID.  

Since you only have one IRQ you only need one index. By setting the first two
parameters to the same value, the IRQ will simply loop and run once every frame. These indexes are used for jumping around between IRQ's in code. They can be modified at runtime as well for very dynamic IRQ switching.  

You can have a timer in the background of the IRQ that will keep track of which frame it is and let you wait for the perfect moment to do something. Setting timer.enabled means you will get a timer tick. IMPORTANT! If you have several IRQ's in one frame, you don't want more than one of the IRQ's to tick. Or the timer will move too fast.  

If you for example want to open the sideborder och create a DYSP, you want to 
have a stable IRQ. In practise, a stable IRQ will generate two IRQ's under the hood, since it's required for the stablizing. You can see that if you check the asm-code.  

You should set the $d012 value where an IRQ is supposed to begin. In this case
you want this IRQ to trigger at line $30 both for this and the next IRQ. Always set it to the same thing when you have a single IRQ.


#### Multiple IRQ's

First of all read how the single IRQ works, since some things won't be repeated here.

`IRQ irq1 = new IRQ(1, 2, Timer.enabled, Stabilize.on, "30", "80");`

`IRQ irq2 = new IRQ(2, 1, Timer.disabled, Stabilize.on, "80", "30");`

This will give you two IRQ's per frame. Let's deep dive;

* IRQ 1 (index 1), and when done will jump to IRQ 2 (index 2).
* Timer is enabled on the first one. Could just as well be enabled on the second one. As long as you don't enable it on more than one per frame.
* Both IRQ 1 and 2 are stabilized. 
* The first one interupts at raster line $30, and the second at $80.


## Example: Two IRQ's changing bg and screen color

```
// First IRQ

IRQ irq1 = new IRQ(1, 2, Timer.enabled, Stabilize.on, "df", "e8");
irq1.asm.d020_D021_SetColor(Color.BLACK_00)
demo.addIRQ(irq1);

// Second IRQ

IRQ irq2 = new IRQ(2, 1, Timer.disabled, Stabilize.on, "e8", "df");
irq2.asm.d020_D021_SetColor(Color.GREEN_05)
demo.addIRQ(irq2);
```

If you want real IRQ examples, I recommend you to check out the example democode prepared in /src/main/java/democode/.
