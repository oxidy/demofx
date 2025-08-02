[Back to Main](../README.md)


## Getting started

This git repo contains a few fully functional Java examples, meaning that you should be able to simply run/debug any of them and they should compile, build, generate a D64-image and finally run the C64 executable in VICE or your Commodore 64/Ultimate 64. The examples should be well commented. These docs will also provide you with useful tips and tutorials.

You should already be familiar with installing the JRE and how to do basic Java development. This won't be explained here.

## Vice support

You can preview your code in VICE - the Versatile Commodore Emulator. To download and install visit: https://vice-emu.sourceforge.io/.  

If you want to use VICE in Linux, you should be able to start it by running `x64` in the terminal. If this doesn't work you still have some configuring to do.  
For more information read the [Ubuntu installation instructions](vice-ubuntu.md).  

For Windows and Mac there are plenty of scenarios and I can't list all here. You could try to exchange the viceRunner with `x64sc` in config.yml, like this:
viceRunner: x64sc

## U64 support 

You can also automatically preview your code directly on your Commodore 64 or Ultimate 64 by using [U64](u64.md). 
This is built into DemoFX, and only needs two lines of configuration to enable. 

## The Java Examples

Below src/main/java/democode you have a few demo examples.

- BasicBitmapShower
- FLDExample
- JohansScroller
- SpindleLoader

And maybe more, I'll be adding examples from time to time. The idea is to create examples for various DemoFX concepts, so you'll have a basic understanding and then you can use that to create much cooler things. When you understand DemoFX you'll have a powerful tool at your fingertips which will speed up creating your own multi-load mega demo. 

All democode exaples have a main method. That's your entry point. Run it, and if you've configured VICE correctly you should have some C64 code running in VICE.
