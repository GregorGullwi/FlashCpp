# Known Issues

## Unity Debug Build Broken (g++ single-unity target only)

The `x64/Debug/FlashCpp` unity build does not compile when using g++:
- **g++:** `applyDeclarationArrayBoundsToTypeSpec` defined but not used in `Parser_Expr_PrimaryExpr.cpp`
  (Warning treated as error via `-Werror`)

**Workaround:** Use `make sharded CXX=clang++` and `x64/Sharded/FlashCpp` for all testing.
The 4-file sharded unity build with clang++ is fully functional.
