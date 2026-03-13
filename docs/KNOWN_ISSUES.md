# Known Issues

This file tracks currently open issues only. Fixed items are removed once they are
validated.

## Missing semantic analysis pass for implicit standard conversions

FlashCpp has no dedicated semantic analysis (Sema) pass between parsing and codegen.
Implicit C++ standard conversions (arithmetic promotions, integral↔floating-point
conversions, etc.) are therefore handled ad-hoc inside individual codegen functions
(`generateBinaryOperatorIr`, return-statement codegen, direct function-call argument
lowering) but are absent in others.

Known gaps:
- Assignments and variable initializers may have the same gap for mixed-type pairs.
- Conditions and other implicit-bool contexts still rely on local lowering rules rather
  than a single semantic normalization step.

Correct fix: introduce an `ImplicitCastNode` AST node and a `SemanticAnalysis` pass
(run after parsing, before codegen) that inserts it wherever C++ [conv] standard
conversions apply — mirroring Clang's `Sema::ImpCastExprToType`.  This would replace
all current ad-hoc `generateTypeConversion` call sites and cover every context
uniformly.

## Parser recursion/iteration limits on deeply nested expressions

- Extremely deep unary-expression nesting can still overflow the parser stack and crash
  the compiler before semantic analysis/constexpr evaluation runs.
- Very long flat binary-expression chains can hit `MAX_BINARY_OP_ITERATIONS` in
  `parse_expression`, which currently surfaces as a parse failure instead of a more
  graceful depth/complexity diagnostic.
- This was observed while trying to build a deep `noexcept(...)` stress test: the new
  constexpr evaluator recursion guard works, but the parser can still fail first on
  sufficiently deep source expressions.

## Enum overload resolution ranks enum as int

When both `f(int)` and `f(Color)` overloads exist and `Color c` is passed, the
compiler currently calls `f(int)` instead of `f(Color)`.  Per C++20
[over.ics.rank], exact match (enum→enum) should be preferred over promotion
(enum→int).  This is related to the missing semantic analysis pass for implicit
standard conversions (see above) — enum identity is not preserved through the
overload resolution ranking step.  Test: `test_enum_overload_resolution_ret0.cpp`.
