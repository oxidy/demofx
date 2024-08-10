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

        demo.bg.code.d020_D021_SetColor(Color.BLACK_00);
        demo.bg.code.d011_DEFAULT_1B();
        demo.bg.add(CodeBase.moveFullscreenToD800("5800"));
        demo.bg.memFill("d800+(40*22)", "01", 40);
        demo.bg.memFill("3400+(40*22)", "20", 40);
        
        // ---------------------------------------------------------------
        IRQ irq1 = new IRQ(1, 2, Timer.enabled, Stabilize.on, "df", "e8");
        irq1.code.delayX("08")
            .code.dd00_KRILLSAFE_Set_Bank0_0000_3fff()
            .code.d018_SetCharmem("3800")
            .code.d018_SetScreen("3400")
            .code.d011_BitAction(D011BitAction.TEXT_MODE_DISABLE_BIT_5)
            .code.stazp("d016", "10");
        demo.addIRQ(irq1);
        // ---------------------------------------------------------------
        IRQ irq2 = new IRQ(2, 1, Timer.disabled, Stabilize.on, "e8", "df");
        irq2.code.delayX("08")
            .code.d011_DisableScreen_7B()
            .code.dd00_KRILLSAFE_Set_Bank1_4000_7fff()
            .code.d018_SetScreenAndBitmap("5c00", "6000")
            .code.d016_BINARY_SetMultiColorMode38Column()
            .code.d011_EnabledScreen_BitmapMode_3B()
            .callWithInterval("scroll1x1", "02");
        demo.addIRQ(irq2);
        // ---------------------------------------------------------------

        demo.addJSR("*", "scroll1x1", 
            CodeBase.d016_Scroll_1X1("10", "3400+(40*22)", "scrolltext"));
    
        demo.injectFileInCode("additional.asm");

        demo.compileAndRun();

	}

}
