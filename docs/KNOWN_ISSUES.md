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

---

## Built-in subscript via implicit pointer conversion on const references is incorrect

**Status:** Fixed in PR-1375  
**Area:** Sema / Codegen — `generateArraySubscriptIr` / `normalizeBuiltinSubscriptOperands`  
**Discovered during:** Implementing follow-up tests for PR-1373 (subscript pointer conversion)

When a class has a pointer conversion operator and an instance is accessed through
a `const T&` reference, the subscript operator call via the implicit conversion
is applied to a spurious copy (the reference is dereferenced and the value is stored
on the stack) rather than to the original object. This causes incorrect results.

```cpp
struct DualConv {
    int nc_data[2];
    int c_data[2];
    operator int*() { return &nc_data[0]; }
    operator const int*() const { return &c_data[0]; }
};

int main() {
    DualConv d;
    d.c_data[0] = 5;
    const DualConv& cd = d;
    return cd[0];  // returns 0 instead of 5
}
```

The non-const case (`d[0]`) works correctly. The bug is isolated to references
(both `const T&` and `T&`) in the subscript pointer conversion path.

**Root cause:** When `visitExpressionNode` resolves an identifier that is a
reference type, it may load (dereference) the reference value rather than pass
the referenced object's address directly to `emitConversionOperatorCall`. The
`this` pointer becomes a pointer to a stack temporary that holds the referenced
object's first bytes rather than the object itself.
