# Known Issues

This file tracks currently open issues only. Fixed items are removed once they are
validated.

## Constexpr pointer: `array_elements` field reused for pointer value snapshot

`EvalResult::array_elements` is semantically defined for array support, but
is also used to carry the pointer value snapshot (at most one element) when
`pointer_to_var.isValid()`. Code that reads `array_elements` while also
checking `is_array` is safe; code that reads it based only on emptiness should
also check `!pointer_to_var.isValid()` to avoid misinterpreting a pointer
snapshot as array data. A dedicated `pointer_value_snapshot` field would be
the long-term fix (tracked as tech debt).

## Inherited struct-typed static members from template bases can keep pattern-qualified aliases

When a non-template derived class inherits a struct-typed static member from a
template base, codegen currently emits both the instantiated base symbol
(`Base$hash::payload`) and an inherited alias on the pattern name
(`Base::payload`). The derived-class default initializer can now be fixed to
load from the actual owner (`Base$hash::payload`), but the extra pattern alias
still indicates that inherited static-member emission is mixing instantiated and
pattern-qualified names.

Observed while adding regression coverage for:

```cpp
template <typename T, int N>
struct Base {
    struct Payload { int a; int b; };
    static constexpr Payload payload = { N, int(sizeof(T)) };
};

struct Derived : Base<int, 9> {
    int value = payload.a + payload.b;
};
```

The narrowed non-template regression now passes after qualifying inherited
member access with the real owner struct, but the underlying inherited-static
definition path in `generateStaticMemberDeclarations()` still deserves cleanup
so template-base aliases are emitted only under the instantiated owner.

## `constexpr`/`consteval` enforcement — partially implemented

C++20 requires that a `constexpr` variable's initializer be a constant expression;
failure is a compile error, not a warning.  C++20 also requires that a `consteval`
(immediate) function is *only* callable in constant-evaluated contexts.

FlashCpp currently:
* parses both `constexpr` and `consteval` specifiers and records them in the AST
  (`is_constexpr()` / `is_consteval()`)
* enforces compile errors for constexpr pointer violations tagged with
  `EvalErrorType::NotConstantExpression` (e.g. ptr+ptr, OOB dereference,
  relational comparison of different-array pointers)
* **enforces `consteval` call sites** (implemented): when a `consteval` function is
  called in a runtime (non-constant-evaluated) context, FlashCpp now attempts
  compile-time evaluation first; if the call cannot be reduced to a constant the
  compiler emits a hard `CompileError`.  This covers:
  - free functions in namespaces and at global scope
  - non-type-parameter template `consteval` functions
  - `consteval` member functions of structs
  - functions returning native scalar types (int, bool, char, float, double) and
    simple aggregate/struct types
* **consteval functions accepted by the constexpr evaluator** (implemented): the
  evaluator now accepts `consteval` functions wherever it previously required
  `constexpr`, including in `static_assert`, constexpr variable initializers, and
  template instantiation.
* **aggregate-initializer returns** (implemented): `return {x, y}` in a
  struct-returning constexpr/consteval function is now evaluated correctly by
  threading the function's return-type info into the evaluation context.
* does **not** enforce the general case for `constexpr` variables:
  - a `constexpr` global variable whose initializer fails constant evaluation
    for evaluator-limitation reasons (`EvalErrorType::Other`) still produces a
    `[WARN][Codegen] Non-constant initializer` warning and zero-initializes
    the variable instead of issuing a compile error

The reason full `constexpr` enforcement is deferred is that `ConstExpr::Evaluator`
cannot yet reliably distinguish "this expression is genuinely not a constant
expression" from "this expression *would* be constant but FlashCpp's evaluator does
not support it yet".  Throwing a hard error on every `Other`-type evaluation failure
would produce false positives for valid C++20 programs that exercise unsupported
evaluator features.

**Future task**: continue tagging evaluator errors as `NotConstantExpression` when
they represent true C++ violations (not evaluator gaps), then the existing enforcement
path in `evalToValue` in `IrGenerator_Stmt_Decl.cpp` will automatically upgrade them
to compile errors.
