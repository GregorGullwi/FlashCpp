# Known Issues

## `<limits>` char32_t extrema still log recoverable oversized return skips

After the shift-result semantic fix, `tests/std/test_std_limits.cpp` compiles
successfully on Linux/libstdc++-14, but codegen still logs two recoverable
`Return value exceeds 32-bit limit` errors while skipping generated helper
functions for the widest character extrema. This is no longer the Phase 15
missed-conversion blocker; it is a remaining codegen/constant materialization
limit for large unsigned character values.

## Unity Debug Build Broken (g++ single-unity target only)

The `x64/Debug/FlashCpp` unity build does not compile when using g++:
- The old `applyDeclarationArrayBoundsToTypeSpec` unused-function warning in
  `Parser_Expr_PrimaryExpr.cpp` is fixed.
- The build is still broken under `make CXX=g++` because g++ now reports many
  other `-Werror` diagnostics instead, including `-Wshadow` warnings from the
  bundled `elfio` headers and multiple `-Wdangling-reference` warnings in
  FlashCpp sources such as `IrGenerator_Expr_Conversions.cpp`.

**Workaround:** Use `make sharded CXX=clang++` and `x64/Sharded/FlashCpp` for all testing.
The 4-file sharded unity build with clang++ is fully functional.
