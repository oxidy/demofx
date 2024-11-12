[Back to Main](../README.md)  |  [Back to Usage](usage.md)  

## Injects

When compiling the Demo object, a KickAsm compatible asm-file is generated. But you will most likely have your own code as well, and this file can be appended to the generated asm-file, like this:

```
demo.injectFileInCode("additional.asm");
```

