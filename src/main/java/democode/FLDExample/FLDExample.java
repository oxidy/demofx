package FLDExample;

import demofx.*;
import demofx.enums.*;

public class FLDExample {

    public FLDExample() {}

    public static void main( String[] args ) {
        try {
            DISK_A();
            
        } catch (Exception e) {
            e.printStackTrace();
        }
    }
    
	public static void DISK_A() throws Exception {
		
        DiskSide diskSide = new DiskSide();
        diskSide.setD64Name("fld");
        Demo demo = diskSide.addDemoPart("FLDExample");

        demo.setInitialIRQD012("02")
            .setStartAddress("8300")
            .setFinalPRGName("fld")
            .setCompressMainFile(true);
        
        demo.setSID("flz-demosceniors.sid", "1000", "1003");
        
        demo.include("5800", "gripen_finished.oxp", "Gripen");

        demo.initCode
            .asm.d020_D021_SetColor(Color.BLACK_00)
            .asm.moveFullscreen("5800", "d800")
            .asm.staa("7f3f", "00")     // Eliminate oxp-bug
            .asm.staa("7fff", "ff");    // Last byte of current video memory

        // ---------------------------------------------------------------
        IRQ irq1 = new IRQ(1, 1, Timer.enabled, Stabilize.on, "02", "02")
            .asm.dd00_KRILLSAFE_Set_Bank1_4000_7fff()
            .asm.d011_EnabledScreen_BitmapMode__BYTE_3b()
            .asm.d016_SetMultiColorMode40Column()
            .asm.d018_SetScreenAndBitmap("5c00", "6000")
            .jsr("fld")
            .playMusic("1003")
            .asm.d011_BitAction(D011BitAction.SCREEN_HEIGHT_24_ROWS_DISABLE_BIT_3);
        demo.addIRQ(irq1);
        // ---------------------------------------------------------------

        demo.injectFileInCode("fld.asm");

        demo.compileAndRun();

	}

}
