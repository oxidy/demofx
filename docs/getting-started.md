[Back to Main](../README.md)


## Getting started

This git repo contains a few fully functional Java examples, meaning that you should be able to simply run/debug any of them and they should compile, build, generate a D64-image and finally run the C64 executable in VICE.

You should already be familiar with installing the JRE and how to do basic Java development. This won't be explained here.

Less obvious is VICE - the Versatile Commodore Emulator.
If you haven't already please visit https://vice-emu.sourceforge.io/ and install VICE.

In Linux, you should be able to start it by running `x64` in the terminal. If not you still have some configuring to do. Please read these [Ubuntu installation instructions](vice-ubuntu.md) 

For Windows and Mac you need to google if you have issues installing and running VICE, but you'll probably find useful clues by reading the previous link.

## The Java Examples

Below src/main/java/democode you have a few demo examples.

- FLDExample
- JohansScroller
- SpindleLoader

And maybe more, I'll be adding examples from time to time. The idea is to create examples for various DemoFX concepts, so you'll have a basic understanding and then you can use that to create much cooler things. When you understand DemoFX you'll have a powerful tool at your fingertips which will speed up creating your own multi-load mega demo. 

All democode exaples have a main method. That's your entry point. Run it, and if you've configured VICE correctly you should have some C64 code running in VICE.
