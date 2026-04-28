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

## `sizeof...` can survive into codegen inside class-template member templates

**Status:** Open
**Area:** Template substitution / late normalization
**Discovered during:** 2026-04-28 phase-6 pack-binding cleanup follow-up

In a non-static member template inside a class template, a pack-size expression
for the member template's parameter pack can remain as `SizeofPackNode` long
enough to reach codegen instead of being substituted during template
instantiation.

```cpp
template <typename... Ts>
struct Holder {
	template <typename... Us>
	int combined_pack_size(Us...) const {
		return static_cast<int>(sizeof...(Ts)) * 10
			 + static_cast<int>(sizeof...(Us));
	}
};

int main() {
	Holder<int, char> holder;
	return holder.combined_pack_size(0L, static_cast<short>(0), true) == 23 ? 0 : 1;
}
```

Current failure:

```text
[ERROR][Codegen] sizeof... operator found during code generation - should have been substituted during template instantiation
```

This is adjacent to the pack-binding cleanup but is not fixed by the shared
named-pack lookup alone. The remaining gap is in member-template body
normalization/materialization rather than the central `sizeof...` count lookup.
