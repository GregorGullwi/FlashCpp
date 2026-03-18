# Known Issues

This file tracks currently open issues only. Fixed items are removed once they are
validated.

## Parser recursion/iteration limits on deeply nested expressions

- Extremely deep unary-expression nesting can still overflow the parser stack and crash
  the compiler before semantic analysis/constexpr evaluation runs.
- Very long flat binary-expression chains can hit `MAX_BINARY_OP_ITERATIONS` in
  `parse_expression`, which currently surfaces as a parse failure instead of a more
  graceful depth/complexity diagnostic.
- This was observed while trying to build a deep `noexcept(...)` stress test: the new
  constexpr evaluator recursion guard works, but the parser can still fail first on
  sufficiently deep source expressions.

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

## Unscoped enum enumerator access through type aliases

Accessing unscoped enum values using a type alias as the qualifier
(`Container::AliasStatus::Ok` where `AliasStatus = Status`) does not work.
The alias `TypeInfo` does not carry an `EnumTypeInfo` (enumerators are only
tracked on the original enum's `TypeInfo`).

```cpp
struct Container {
    enum Status { Ok, Fail };          // unscoped
    using AliasStatus = Status;
};
// Works:    Container::Status::Ok
// Broken:   Container::AliasStatus::Ok  (no EnumTypeInfo on alias)
```

Scoped enums (`enum class`) work through aliases because the enumerator lookup
uses a different code path.

**Workaround**: use the original enum name (`Container::Status::Ok`) or `enum class`.
