## Introduction  

This repository is a sample project to demonstrate how to use the DemoFX library.  

DemoFX is a java framework used for building C64 demos.  
All DemoFX development is done by Mikael Lilja, aka Oxidy/FairLight.  
Current version of DemoFX is v1.0.0.  

DemoFX development started in 2018, and has been ongoing since. To make sure it was actually  
useful a two disk-side trackmo was developed in parallel. This demo was Skaaneland 2 by FairLight.  

You can check Skaaneland 2 here: https://www.youtube.com/watch?v=eDRi9r10HXU  

DemoFX's primary purpose is to manage IRQ's, IRQ-loading, packing/depacking, linking and timelines.  
Additionally there is extensive support for built in macros. You will of course still need to  
do effects and various code in assembler, but you will at least get rid of all the boring  
time consuming stuff.  

Everything is automatic. When you run your project, it will generate all the 6510 assembler code,   compile and pack it, manage all includes, prepare the D64 and launch the final project  
in VICE. The only thing you need to do to get this working is to follow the structure of this  
sample project and to update the paths in config.yaml.  

In the end you will have a working D64, with a demo that can run on the real C64 hardware.  


## Support

I'm happy to help, but initially I'm quite certain that most of your questions will be answered  
when I have managed to create more samples and documentation. This is work in progress.  
Please keep an eye out for updates.


## External libraries

DemoFX uses some external tools, and the authors of these need to be mentioned.  

The assembler is KickAsm by Mads Nielsen:  
http://theweb.dk/KickAssembler/  

The emulator is VICE:  
https://vice-emu.sourceforge.io/  

The packer used is ByteBoozer by HCL of Booze Design  

DemoFX currently supports two loaders.  
- The Krill loader by Krill of Plush  
- Spindle by LFT  
Depending on which one you use, it could be prudent to mention this in the credits.  


## BitmapFragments and DemoFX Studio  

There is a concept introduced in DemoFX which is called BitmapFragments.  
Using these you can easily do bitmap animations and manipulations.  
Basically, you convert some part of a bitmap to a packed piece of data which can be  
merged, loaded, unpacked and repeatedly shown anywhere on screen with almost no effort.  

BitmapFragments were for example heavily used in Skaaneland 2 to manipulate graphics.  

To create BitmapFragments you will need DemoFX Studio. This is a bitmap editor/animator and  
it will be released shortly. Useful DemoFX samples will be added that explains everything  
there is to know about BitmapFragments.  


## Quick start

- Clone this repository  
- Open folder in VS Code or similar  
- Make sure you have at least Java 8 JDK installed  
- Update `config.yaml` to reflect your paths  
- Run a sample  

If everything is set up correctly, VICE will launch, starting a D64 with your demo.  

Keep in mind, when cloning an updated version of this repository you will lose your  
config.yaml changes. Do a renamed backup of this file just in case.  


## Documentation  

Documentation is more or less missing at this point. Everything will of course be documented  
in detail, but my first focus is to make working samples. All samples will be well commented  
and should give you a quick understanding of the possibilities.  


## Folder Structure  

The demodev workspace contains several important folders and files:  

- `/output`             : the folder where generated files will be placed.  
- `/output/d64`         : the folder where generated d64-files will be placed.  
- `/output/temp`        : temporary files used while building the prg-files and d64.  
- `/democode`           : Your C64 project folder.  
- `/democode/src`       : Source code.  
- `/democode/lib`       : Folder for C64-related binaries and tools. Modify content at your own risk.  
- `/demofx`             : The C64 Demo Framework.  
- `/repository`         : Common include files for all projects, such as sids, fonts, gfx.  
- `/repository/sids`    : Common sids.  
- `/repository/fonts`   : Common fonts.  
- `/repository/gfx`     : Common gfx.  

- `/config.yaml`        : Demodev configuration file.  

- `{demoDevPath}`                           : package  
- `{demoDevPath}/{projectname}`             : demodev project folder.  
- `{demoDevPath}/{projectname}/includes`    : default project include files.  

demoDevPath is configured in config.yaml.  
