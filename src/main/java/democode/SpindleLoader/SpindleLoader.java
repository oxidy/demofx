/**
 *  -------------------------------------------------------------------------------
 * 	 DemoFX SpindleLoader example by Oxidy/FairLight
 *  -------------------------------------------------------------------------------
 * 	This code will load a bitmap image into memory using the Spindle loader by LFT.
 *  Nothing beautiful, but it will give you a minimal demo using an IRQ loader.
 * 	The background color will indicate the different steps during the process.
 * 	Blue:	Timeline waits 5 seconds
 * 	Gray:	Load bitmap image. You will see the bitmap appear as it loads.
 * 	Black:	File finished loading and the color data is moved to color memory.
 *  -------------------------------------------------------------------------------
 */
package SpindleLoader;

import demofx.*;
import demofx.core.DemoFxCore;
import demofx.enums.Color;
import demofx.enums.DirArtType;
import demofx.enums.LoaderType;

public class SpindleLoader {    

	DiskSide diskSide = new DiskSide();
	DemoFxCore core = new DemoFxCore(diskSide);

    public SpindleLoader() {}

    public static void main( String[] args ) throws Exception {
        SpindleLoader sl = new SpindleLoader();
        sl.A_SpindleLoader();
    }

    public void A_SpindleLoader() throws Exception {
		
		// 
        DiskSide diskSide = new DiskSide();
        diskSide.setD64Name("spindle_disk_a");
        diskSide.setMainFileToLoad("spindlemain");
        diskSide.setMainFileAddress("8000");

        Demo demo = diskSide.addDemoPart("SpindleLoader");
		demo.finalPRGName = "spindlemain";

		// Select the Spindle loader.
		demo.setLoaderType(LoaderType.SPINDLE_BY_LFT);

		// Spindle dirart settings
		demo.setDirArtType(DirArtType.PETMATE_JSON_EXPORT)
			.setDirArt("flt-gubbdata-dirart.json");

		demo.setCompressMainFile(true)
			.setInitialIRQD012("f8")
			.setStartAddress("8000");

		// Load a SID. The path should be either SpindleLoader/includes/ or repository/sids/.
		demo.setSID("flz-demosceniors.sid", "1000", "1003");
		
		// Save a bitmap as a loadable spindle file. 
		//
		// In this case the bitmap has loading bytes. Let's remove them.
		// The prepared file will end up in the temp folder with a .noload extension.
		//demo.prepareFile("predator_cut_out_scenes.prg", PrepareFileType.REMOVE_LOADING_BYTES);
		core.tools.removeLoadingBytes("SpindleLoader/includes/predator_cut_out_scenes.prg");
		
		// Now add the file to be loaded by Spindle. The keepLoading flag controls loading additional files.
		diskSide.addSpindleFile("5800", "predator_cut_out_scenes.prg.noload", false);

		// Create a simple IRQ and add some code.
		// In this example we only make use of predefined functions. 
		// All predefined functions should be available in the irq or the irq.asm object
		// ---------------------------------------------------------------
		demo.addIRQ(new IRQ(1, "f8")
			.label("colr:")
			.asm.d020_D021_SetColor(Color.BLUE_06)
			.asm.dd02_SPINDLESAFE_Set_Bank1_4000_7fff()
			.asm.d018_SetScreenAndBitmap("5c00", "6000")
			.asm.d016_SetMultiColorModeWithNoScrolling()
			.asm.d011_EnabledScreen_BitmapMode__BYTE_3b()
			.asm.d011_Set25Rows()
			.playMusic(demo.musicPlayAddress));

		demo.addIRQ(new IRQ(2, "f8")
			.label("colr2:")
			.asm.d020(Color.DGRAY_11)
			.playMusic(demo.musicPlayAddress));
        // ---------------------------------------------------------------
		
		// This is background code, which will always run outside the IRQ's.
		// Anything you add to the demo.bg object will be run sequentially.
		
		// First we use the timeline functionality to wait.
		// No need to count frames. Note that the format is: HH:MM:FRAMES
		// Possible values: HH = 00-99, MM = 00-59, FRAMES = 0-49
		demo.bg.time.timelineWait("00:05.00");

		// Modify the background color to RED (hex "0b") using the label in the IRQ. 
		// staa is basically a LDA + STA macro.
		demo.bg.asm.staa("colr+1", "02");
		
		// Load the bitmap
		demo.bg.spindleLoad();

		// Change the background color again to black (00), this time using the Color enum.
		demo.bg.asm.staa("colr+1", Color.BLACK_00);

		// Move color data from 5800 to $d800.
		demo.bg.asm.moveFullscreenToD800("5800");

		// This is for showing how to change from one IRQ to another.
		// Note that this is 15 seconds into the timeline. Not to be confused with a delay.
		demo.bg.timelineWait("00:15:00");
		demo.bg.changeIRQPointers("1", "2", "f8");

		// Compile the demopart
        demo.compile();

		// Create the spindle script, and load the d64 in VICE
        diskSide.compileSpindleScript();
		diskSide.runDiskInVICE();

	}

}
