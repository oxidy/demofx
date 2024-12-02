package BasicBitmapShower;

import demofx.*;
import demofx.enums.*;

public class BasicBitmapShower {

    public BasicBitmapShower() {}

    public static void main( String[] args ) {
        try {
            /*
             * Create a demo object. 
             *      Note that method chaining makes it possible to continue setting properties.
             */
            Demo demo = new Demo("BasicBitmapShower")
                // The code should start at memory address $0810
                .setStartAddress("0810")
                // Let's set the interupt to $30.
                .setInitialIRQD012("30")
                // We want to pack our small "demo" to save some space on the disk
                .setCompressMainFile(true);
    
            /*
             * Let's load the bitmap into memory.
             * The expected bitmap format looks like this:
             *      $5800-$5bff - Color data
             *      $5c00-$5fff - Screen colors
             *      $6000-$7f40 - Bitmap data
             */
            demo.include("5800", "predator_cut_out_scenes.prg", "predator");
    
            /*
             * The color data must be moved to $d800
             */
            demo.initCode.asm.moveFullscreenToD800("5800");
    
            /*
             * Create an IRQ and make sure the bitmap shows.
             */
            IRQ irq = new IRQ()
                // Set both background color ($d020) and screen color ($d021) to black.
                .asm.d020_D021_SetColor(Color.BLACK_00)
                // We have our bitmap into bank 1. Let's select that bank.
                .asm.dd00_KRILLSAFE_Set_Bank1_4000_7fff()
                // The bitmap is multicolor. Let's enable that.
                .asm.d016_SetMultiColorMode40Column()
                // Enable bitmap mode (pixels are twice as wide, and therefore can manage colors)
                .asm.d011_EnabledScreen_BitmapMode__BYTE_3b()
                // And finally point to the locations of the screen colors and bitmap data.
                .asm.d018_SetScreenAndBitmap("5c00", "6000");
            demo.addIRQ(irq);

            /*
             * Compile the demo and launch it in VICE
             */
            demo.compileAndRun();
    
        } catch (Exception e) {
            e.printStackTrace();
        }
    }
}
