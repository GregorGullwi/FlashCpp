# Known Issues

## Floating-point array subscript reads return 0

**Status:** Open  
**Area:** Codegen — array subscript IR (`IrGenerator_MemberAccess.cpp`)  
**Discovered during:** Investigation of reversed array subscript support (PR #1372)

Subscripting a local `float` or `double` array and assigning to a floating-point
variable always reads 0 instead of the stored value.

```cpp
int main() {
    double darr[3] = { 1.0, 2.0, 3.0 };
    double x = darr[0];  // x is 0.0, should be 1.0
    if (x > 0.5) return 42;
    return 0;  // returns 0 (wrong), expected 42
}
```

The bug also affects `float` arrays.  Scalar `double`/`float` variables and
cast-from-double work correctly; the fault is isolated to the array-subscript
load path for floating-point element types.

Integer arrays (`int`, `short`, `long`, etc.) are unaffected.

**Root cause:** The `ArrayAccess` IR instruction or the load it emits does not
handle 64-bit (double) or 32-bit (float) element types correctly — likely using
the wrong load size or IR type for floating-point elements in the array access
code path in `IRConverter_ConvertMain.cpp` / `IrGenerator_MemberAccess.cpp`.

