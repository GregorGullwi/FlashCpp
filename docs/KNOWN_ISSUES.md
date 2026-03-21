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

## Constexpr pointer: snapshot semantics vs. live reference semantics

When `&x` is evaluated where `x` is a local constexpr variable (in bindings),
the pointer captures a **snapshot** of the value at that point in time.
In C++ constexpr evaluation, a pointer should observe subsequent mutations
to the pointed-to object. For example:

```cpp
constexpr int f() {
    int x = 1;
    int* p = &x;
    x = 2;
    return *p; // C++ requires 2; FlashCpp returns 1 (snapshot)
}
```

Since constexpr variables are immutable and mutable local variables in
constexpr contexts are rare, this gap is not triggered by typical usage.
The snapshot approach is an implementation trade-off to enable cross-scope
pointer passing without a full reference-cell model.

## Constexpr pointer: `array_elements` field reused for pointer value snapshot

`EvalResult::array_elements` is semantically defined for array support, but
is also used to carry the pointer value snapshot (at most one element) when
`pointer_to_var.isValid()`. Code that reads `array_elements` while also
checking `is_array` is safe; code that reads it based only on emptiness should
also check `!pointer_to_var.isValid()` to avoid misinterpreting a pointer
snapshot as array data. A dedicated `pointer_value_snapshot` field would be
the long-term fix (tracked as tech debt).

## Assignment operator finders: `params.size() == 1` guard not relaxed

`findCopyAssignmentOperator()` and `findMoveAssignmentOperator()` in
`AstNodeTypes.cpp` still use `params.size() == 1` in their slow-path loops.
Assignment operators with trailing default arguments (e.g.
`Foo& operator=(const Foo&, int = 0)`) are extremely rare in practice and are
not recognized by these helpers. If such operators are ever used, the slow path
will fail to match and the operator will not be found. The same applies to the
assignment operator refinement in `Parser_Decl_StructEnum.cpp:2806`.
(Note: the deleted assignment operator detection at `Parser_Decl_StructEnum.cpp:2155`
was fixed to use `computeMinRequiredArgs(params) <= 1` in the post-Phase-18 cleanup.)

## Boolean negation + struct member access in chained if-statements

A pre-existing codegen bug causes incorrect results when `!obj.bool_member` is
used in an `if` statement preceding another `if` that reads a different member
of the same struct. The workaround is to use `int` flags instead of `bool`
members, or avoid chaining `if (!obj.bool_field)` with subsequent member
accesses. Observed during copy/move constructor regression testing.

## `constexpr`/`consteval` enforcement not yet implemented

C++20 requires that a `constexpr` variable's initializer be a constant expression;
failure is a compile error, not a warning.  C++20 also requires that a `consteval`
(immediate) function is *only* callable in constant-evaluated contexts.

FlashCpp currently:
* parses both `constexpr` and `consteval` specifiers and records them in the AST
  (`is_constexpr()` / `is_consteval()`)
* does **not** enforce either rule:
  - a `constexpr` global variable whose initializer fails constant evaluation
    produces a `[WARN][Codegen] Non-constant initializer` warning and zero-initializes
    the variable instead of issuing a compile error
  - a `consteval` function is treated identically to a `constexpr` function
    (may be called at runtime without error)

The reason enforcement is deferred is that `ConstExpr::Evaluator` cannot yet
reliably distinguish "this expression is genuinely not a constant expression" from
"this expression *would* be constant but FlashCpp's evaluator does not support it
yet".  Throwing a hard error on every evaluation failure would produce false
positives for valid C++20 programs that exercise unsupported evaluator features.

**Future task**: once the evaluator coverage is broad enough, change `evalToValue`
in `IrGenerator_Stmt_Decl.cpp` to call `throw CompileError(...)` when
`node.is_constexpr()` is true and constant evaluation fails, and add a separate
diagnostic for `consteval` call sites outside constant-evaluated contexts.

The five test files below document the desired (currently unenforced) behaviour.
They live in `tests/future/` so the CI test runners do not pick them up as
`_fail.cpp` tests (which would cause "UNEXPECTED PASS" failures). Once
enforcement is implemented, move them back to `tests/`.
* `tests/future/test_constexpr_ptr_arith_fail.cpp`
* `tests/future/test_constexpr_ptr_diff_different_arrays_fail.cpp`
* `tests/future/test_constexpr_ptr_negative_offset_fail.cpp`
* `tests/future/test_constexpr_ptr_oob_deref_fail.cpp`
* `tests/future/test_constexpr_ptr_relational_diff_arrays_fail.cpp`
