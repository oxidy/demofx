# DemoFX - C64 Java Development Framework

DemoFX is a java framework for developing C64 demos.  
The complete demo Skaaneland 2 by FairLight was written using this framework. That was a few years ago, and the framework has evolved a lot since then. 

There's also a tool called DemoFX Plus. A Beta version is included in the /lib-folder. Use this to draw bitmap images, create BitmapFragments or ColorCycle effects.


## Overview

It's main purpose is not to create demo effects for you, but to provide a simplified way of managing IRQ's, timing, timelines, IRQ-loading. It also have plenty of convenient functions that help you generate standard code and move things around in memory.  

If set up correctly, your java project will generate a KickAsm-compatible source code file which will be automatically built, copied into a D64, and run in VICE.


## Table of Contents

- [Getting started](docs/getting-started.md)
- [Usage](docs/usage.md)
- [Troubleshooting](docs/troubleshooting.md)

## Previewing 

- [VICE Installation Ubuntu](docs/vice-ubuntu.md)
- [U64-support](docs/u64.md)
