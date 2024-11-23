[Back to Main](../README.md)


## Troubleshooting

### Processor architecture issues

In DemoFX there are a few external tools in the lib folder which are needed for building/packing/executing.

These tools require to be built using an OS and processor architecture that matches your system. 

For example, if you are using Windows, you obviously can't run Linux binaries. And if you're using a MacBook M3 arm64 processor you can't use something built for amd64/x86_64.

ByteBoozer 2.0 has currently been compiled for  
arm64 - Linux  
arm64 - MacOS  
amd64 - Windows

If you should have any problems running ByteBoozer 2.0 (b2), it should show in the console log. To get support, contact: micke at oxidy dot net. 

### MacOS issues

When trying to run DemoFX on MacOS, some external tools might be blocked from execution.

This for example happens with ByteBoozer (b2).

After running it once, you can let MacOS allow b2 by going into System Settings and then Privacy & Security.
There you will have a question about running that application, and you will have to allow it.

