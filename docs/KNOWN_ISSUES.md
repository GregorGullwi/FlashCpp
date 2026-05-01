# Known Issues

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
