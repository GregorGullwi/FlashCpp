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

## Generic lambda `auto` parameter normalization still lives in codegen

Generic lambda bodies now receive synthetic parameter declarations carrying the
deduced `TypeSpecifierNode`, which fixes narrow signed integer regressions such
as `signed char` in `test_generic_lambda_auto_narrow_signed_char_ret0.cpp`.
However, the overall normalization still happens in codegen instead of a
dedicated semantic pass. The long-term architectural fix is still to move this
kind of deduced-parameter and implicit-conversion normalization into a
post-parse semantic analysis stage rather than keep growing codegen-local
fallbacks and synthetic declarations.

## Parenthesized declarator form `T(x)[N]` not supported

FlashCpp does not yet parse the *parenthesized declarator* form of a variable
declaration:

```cpp
template<typename T>
void f() {
    T(x)[3];   // C++20: declares x as T[3] — NOT a cast + subscript expression
    T(y);      // C++20: declares y as T    — NOT a functional cast
}
```

Per C++20 [dcl.ambig.res], when a statement is syntactically ambiguous between
a declaration and an expression, it shall be treated as a declaration.
`T(x)[3]` should therefore declare `x` as `T[3]`, but `parse_variable_declaration`
currently expects a plain identifier directly after the type specifier and does
not recognise the `(identifier)` declarator syntax.

The disambiguation routing in `parse_statement_or_declaration` is **correct**
(it no longer misidentifies `[` as an expression-only token), and a `_fail` test
(`test_tparam_bracket_decl_ambig_fail.cpp`) documents the current parse error.
The remaining work is to extend `parse_variable_declaration` (and the declarator
parser) to handle parenthesized declarators as defined in [dcl.decl]/[dcl.paren].

## Overloaded function resolution with size-differing parameter types

When two overloads differ only in integer width (e.g. `f(int)` vs `f(long)`),
calling the `long` overload with a `0L` literal results in the `int` overload
being selected at runtime.  For example:

```cpp
int f(int x)  { return x; }
int f(long x) { return (int)x + 100; }

int main() {
    f(0L);   // Expected: 100 (long overload). Actual: 0 (int overload selected)
}
```

This is a codegen-level limitation in overload dispatch for integer-width variants.
The semantic pass (`tryAnnotateCallArgConversions`) uses pointer-identity matching to
recover the parser-resolved overload, but when the argument value is `0L` and the int
overload was chosen by the parser the behaviour diverges from the standard.

Tracking: `tests/test_overload_call_annotation_ret0.cpp` avoids this case.

## Struct-iterator range-for result miscompare after `+=`

While fixing range-for `auto` deduction for struct iterators, an additional
codegen/runtime issue showed up in a nearby path: this returns `1` even though
the loop sum itself evaluates to `60` when returned directly.

```cpp
int sum = 0;
for (auto value : c) {
	sum += value;
}
return sum == 60 ? 0 : 1;
```

Workaround: return the arithmetic delta (`return 60 - sum;`) or otherwise avoid
this compare/conditional form on the affected path until the underlying
miscompare is isolated.
