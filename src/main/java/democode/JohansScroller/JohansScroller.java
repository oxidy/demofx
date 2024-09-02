package JohansScroller;

import demofx.*;
import demofx.enums.*;

public class JohansScroller {

    public JohansScroller() {}

    public static void main( String[] args ) {
        try {
            A_BitmapAndScroller();
            
        } catch (Exception e) {
            e.printStackTrace();
        }
    }
    
	public static void A_BitmapAndScroller() throws Exception {
        
		Demo demo = new Demo("JohansScroller");

        demo.setStartAddressAndInitialIRQ("8000", "df")
            .setClearScreenAndColorRAM(true)
            .setCompressMainFile(true)
            .setD64Name("johanscroller")
            .setFinalPRGName("scroll");
	    
        demo.include("5800", "predator_cut_out_scenes.prg", "predator")
            .include("3800", "arena_b.font", "font");

        demo.initCode.asm.d020_D021_SetColor(Color.BLACK_00);

        demo.bg
            .asm.d011_DEFAULT_1b()
            .asm.d011_Set25Rows()
            .asm.moveFullscreenToD800("5800")
            .asm.memFill("d800+(40*22)", "01", 40)
            .asm.memFill("3400+(40*22)", "20", 40);

        // ---------------------------------------------------------------
        IRQ irq1 = new IRQ(1, 2, Timer.enabled, Stabilize.on, "df", "e8");
        irq1.asm.delayX("08")
            .asm.dd00_KRILLSAFE_Set_Bank0_0000_3fff()
            .asm.d018_SetScreenAndCharmem("3400", "3800")
            .asm.d011_Set24Rows()
            .asm.d011_BitAction(D011BitAction.TEXT_MODE_DISABLE_BIT_5)
            .asm.d016_FromZP("10");
        demo.addIRQ(irq1);
        // ---------------------------------------------------------------
        IRQ irq2 = new IRQ(2, 1, Timer.disabled, Stabilize.on, "e8", "df");
        irq2.asm.delayX("08")
            .asm.d011_DisableScreen__BYTE_7b()
            .asm.dd00_KRILLSAFE_Set_Bank1_4000_7fff()
            .asm.d018_SetScreenAndBitmap("5c00", "6000")
            .asm.d016_SetMultiColorMode38Column()
            .asm.d011_EnabledScreen_BitmapMode__BYTE_3b()
            .callWithInterval("scroll1x1", "02");
        demo.addIRQ(irq2);
        // ---------------------------------------------------------------

        demo.addJSR("*", "scroll1x1", 
            CodeBase.d016_Scroll_1X1("10", "3400+(40*22)", "scrolltext"));
    
        demo.injectFileInCode("additional.asm");

        demo.compileAndRun();

	}

}
