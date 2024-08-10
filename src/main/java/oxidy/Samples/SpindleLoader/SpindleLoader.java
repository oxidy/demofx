/**
 *  -------------------------------------------------
 * 	 DemoFX SpindleLoader example by Oxidy/FairLight
 *  -------------------------------------------------
 * 	This code will load a bitmap image into memory using the Spindle loader by LFT.
 *  Nothing beautiful, but it will give you a minimal demo using an IRQ loader.
 * 	The background color will indicate the different steps during the process.
 * 	Blue:	Timeline waits 5 seconds
 * 	Gray:	Load bitmap image. You will see the bitmap appear as it loads.
 * 	Black:	File loaded and color data is moved to the color mem.
 * 
 */
package oxidy.Samples.SpindleLoader;

import demofx.*;
import demofx.enums.Color;
import demofx.enums.DirArtType;
import demofx.enums.LoaderType;
import demofx.enums.PrepareFileType;

public class SpindleLoader {    

    public static void main( String[] args ) {    	    	
        try {
			A_SpindleLoader();
			
		} catch (Exception e) {
			e.printStackTrace();
		}
    }

    public static void A_SpindleLoader() throws Exception {
		
		// Create a demo object. 
		// If the demo is directly below the demoDevPath set in config.yaml like this:
		// 		/democode/src/main/java/oxidy/SpindleLoader
		// Then you can use the following to initiate the demo:
		// 		Demo demo = new Demo("SpindleLoader");
		// But, if you (like in this case) have a subfolder like this:
		//		/democode/src/main/java/oxidy/Samples/SpindleLoader
		// Then you will need to specify the project location like this:
		Demo demo = new Demo("Samples", "SpindleLoader");

		// Select the Spindle loader.
		demo.loaderType = LoaderType.SPINDLE_BY_LFT;
		
		// Spindle dirart settings
		demo.dirArtType = DirArtType.PETMATE_JSON_EXPORT;
		demo.dirArt = "flt-gubbdata-dirart.json";

		demo.d64Name = "spindle_disk_a";
		demo.setCompressMainFile(false);
		demo.initialIRQD012 = "f8";
	    demo.startAddress = "8000";

		// Load a SID. The path should be either SpindleLoader/includes/ or repository/sids/.
		demo.setSID("flz-demosceniors.sid", "1000", "1003");
		
		// Save a bitmap as a loadable spindle file. 
		//
		// In this case the bitmap has loading bytes. Let's remove them.
		// The prepared file will end up in the temp folder with a .noload extension.
		demo.prepareFile("predator_cut_out_scenes.prg", PrepareFileType.REMOVE_LOADING_BYTES);
		// Now add the file to be loaded by Spindle. The keepLoading flag controls loading additional files.
		demo.addSpindleFile("5800", "predator_cut_out_scenes.prg.noload", false);

		// Create a simple IRQ and add some code.
		// In this example we only make use of predefined functions. 
		// All predefined functions should be available in the irq or the irq.code object
		// ---------------------------------------------------------------
		demo.addIRQ(new IRQ(1)
			.label("colr:")
			.code.d020_D021_SetColor(Color.BLUE_06)
			.code.dd00_SPINDLESAFE_Set_Bank1_4000_7fff()
			.code.d018_SetScreenAndBitmap("5c00", "6000")
			.code.d016_BINARY_SetMultiColorModeWithNoScrolling()
			.code.d011_EnabledScreen_BitmapMode_3B()
			.code.d011_BINARY_Set25Rows()
			.playMusic(demo.musicPlayAddress));
        // ---------------------------------------------------------------
		
		// This is background code, always run outside the IRQ's.
		// Anything you add to the demo.bg object will be run sequentially.
		
		// First we use the timeline functionality to wait.
		// No need to count frames. Note that the format is: HH:MM:FRAMES
		// Possible values: HH = 00-99, MM = 00-59, FRAMES = 0-49
		demo.bg.timelineWait("00:05.00");

		// Modify the background color to DARK GRAY (hex "0b") using the label in the IRQ. 
		// staa is basically a LDA + STA macro.
		demo.bg.staa("colr+1", "0b");
		
		// Load the bitmap
		demo.bg.spindleLoad();

		// Change the background color to black (00), this time using the Color enum.
		demo.bg.staa("colr+1", Color.BLACK_00);

		// Move color data from 5800 to $d800.
		demo.bg.moveFullscreenToD800("5800");

		// Compile, create d64, run in Vice.
        demo.compileAndRun();

	}

}
