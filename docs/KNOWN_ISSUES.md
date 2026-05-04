# Known Issues

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
