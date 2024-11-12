[Back to Main](../README.md)  |  [Back to Usage](usage.md)  


## Built-in code - The initCode object

Anything added to the initCode object will be executed before the IRQ starts. This is useful if you want to clear memory or do preparations that otherwise could cause glitches the first few frames.

Here's an example where we move bitmap into color memory. Note that this particular example can be done in two ways. Both does the same thing, but moving a screen specifically to $d800 is such a common thing that it has it's own function.

```
// Move fullscreen to $d800
demo.initCode.asm.moveFullscreen("5800", "d800");

// Move fullscreen to $d800
demo.initCode.asm.moveFullscreenD800("5800");
```

## When to use .initCode and when to use .bg

The `initCode` object only runs once, and before the IRQ starts.

The `bg` (background) object starts when the IRQ's have been initialized, and whatever is done here can control the demo. As the name suggests the `bg` object is the perfect place for all background tasks like IRQ-loading, depacking, memory manipulation, timing, etc.

Sometimes the `bg` object is referred to as the main loop, but that's only correct if you actually loop it.

If the `bg` object doesn't have anything else to do it will run a `jmp *` command which basically stops it. You can control this by jumping somewhere else.

