# Known Issues

## `<limits>` char32_t extrema still log recoverable oversized return skips

After the shift-result semantic fix, `tests/std/test_std_limits.cpp` compiles
successfully on Linux/libstdc++-14, but codegen still logs two recoverable
`Return value exceeds 32-bit limit` errors while skipping generated helper
functions for the widest character extrema. This is no longer the Phase 15
missed-conversion blocker; it is a remaining codegen/constant materialization
limit for large unsigned character values.

## Generic lambda `auto&&` callable parameter lvalue crash

A generic lambda that takes a callable as `auto&&` and returns the result of
invoking an lvalue lambda argument can still compile to a crashing program. A
minimal shape is `[](auto&& f, int v) { return f(v); }(lambda, value)`. The
remaining issue appears to be in generic-lambda return/value-category
normalization for forwarding-reference callable parameters, not in the generic
callable receiver fallback narrowed by the current fix.

## Unity Debug Build Broken (g++ single-unity target only)

The `x64/Debug/FlashCpp` unity build does not compile when using g++:
- **g++:** `applyDeclarationArrayBoundsToTypeSpec` defined but not used in `Parser_Expr_PrimaryExpr.cpp`
  (Warning treated as error via `-Werror`)

**Workaround:** Use `make sharded CXX=clang++` and `x64/Sharded/FlashCpp` for all testing.
The 4-file sharded unity build with clang++ is fully functional.
