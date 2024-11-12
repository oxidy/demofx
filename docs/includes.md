[Back to Main](../README.md)  |  [Back to Usage](usage.md)  

## Includes

Any binary file such as bitmaps, sids, fonts, can be included in memory. Here's an example of a bitmap:

```
demo.include("5800", "bitmap.prg", "MyBitmapTitle");
```

| Name         | Explanation  |  
|--------------|--------------|  
| memLocation | Where in memory to put the file |  
| fileName | File to include |  
| compileTag | Shows when KickAsm compiles |  

## In-depth explanation

Basically the function will generate a standard KickAsm-import. If you'd be writing all asm-code yourself the include above would be equivalent to this:

```
.pc = $5800 "MyBitmapTitle"
.import binary "/home/user/Path/to/includes/bitmap.prg"
```
Note that DemoFX uses method chaining in many situations including this one, so you are able to make as many subsequent calls as you wish on the returning object like this:

```
demo.include("5800", "bitmap.prg", "MyBitmapTitle")
    .include("3800", "1x1.font", "My1x1Font");
```
