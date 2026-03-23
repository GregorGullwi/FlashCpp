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

## Boolean negation + struct member access in chained if-statements

A pre-existing codegen bug causes incorrect results when `!obj.bool_member` is
used in an `if` statement preceding another `if` that reads a different member
of the same struct. The workaround is to use `int` flags instead of `bool`
members, or avoid chaining `if (!obj.bool_field)` with subsequent member
accesses. Observed during copy/move constructor regression testing.

## Direct member access on prvalue struct temporaries can crash at runtime

Accessing a struct member directly from a prvalue temporary can compile and link
but produce a runtime stack overflow in the generated program:

```cpp
struct Box {
    int value;
    Box(int x) : value(x) {}
};

int f() {
    return Box(7).value; // observed runtime crash (0xC00000FD)
}
```

This was observed while writing a template constructor-overload regression; both
`Box(N).value` and `Box{N}.value` hit the same nearby failure mode. The symptom
looks like a temporary-object/member-access codegen bug rather than a semantic
analysis error.

**Workaround**: avoid direct member access on a prvalue temporary; pass the
temporary through a helper function or otherwise materialize/access it through a
different path.

## Nested template static members of struct type can misbehave at runtime

Struct-typed static members inside nested template classes are still unreliable
at runtime. Inline/`constexpr` forms can read back as zero-initialized even when
their template-substituted initializer should produce non-zero field values, and
mutable storage variants can crash when written through:

```cpp
template<typename U, class C, int N>
struct Outer {
    struct Payload { int a; int b; };
    struct Inner {
        static constexpr Payload payload = { int(sizeof(C) - sizeof(U)), N };
        int value = payload.a + payload.b;
    };
};

int main() {
    Outer<char, int, 39>::Inner inner{};
    return inner.value + Outer<char, int, 39>::Inner::payload.a; // observed 0, expected 45
}
```

Observed while trying to add focused validation around nested static-member
substitution/layout. This looks like a remaining nested-class/static-object
codegen/runtime bug rather than a parser/sema typing failure.

**Workaround**: avoid struct-typed static members inside nested template
classes; use scalar static members, move the object out of the nested template
class, or materialize the value through another helper path.

## Conversion operator type-alias resolution incomplete during template instantiation

When a conversion operator uses a dependent type alias (e.g.,
`operator value_type()` where `using value_type = T;`), template instantiation
may not fully resolve the alias, leaving the member function registered under the
internal name `"operator user_defined"` instead of the canonical form
`"operator int"`, `"operator double"`, etc.

Both sema (`structHasConversionOperatorTo` in `SemanticAnalysis.cpp`) and codegen
(`findConversionOperator` in `IrGenerator_MemberAccess.cpp`) work around this by:

1. resolving the `UserDefined` return type through the `gTypeInfo` alias chain
2. matching the resolved return type against the target type
3. falling back to **size-based matching** when alias resolution fails

The size-based fallback is not standard-conformant: it can match wrong types that
share the same bit width (e.g., `int` and `float` are both 32-bit; `double` and
`long long` are both 64-bit). In practice this has not caused observable
miscompilations because the fallback only fires for still-unresolved `UserDefined`
return types in template contexts, but it is a known accuracy limitation.

The proper fix is to fully resolve type aliases during template instantiation so
that conversion operators always carry their canonical name and the
`"operator user_defined"` workaround becomes unnecessary.

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


## Float member store incorrect when using static_cast<float>(double_ref) in constructor

When a constructor takes a `const double&` parameter and assigns
`this->float_member = static_cast<float>(d)`, the codegen emits the correct
`cvtsd2ss` conversion but then stores from an uninitialized integer register
(`r9d`) instead of from the converted XMM result, leaving the member with
garbage data:

```cpp
struct FloatTarget {
    float value;
    FloatTarget(const double& d) : value(static_cast<float>(d)) {}
};

int main() {
    FloatTarget f = 3.14;   // f.value is indeterminate — not 3.14f
    return f.value > 3.0f ? 0 : 1; // unexpectedly returns 1
}
```

The disassembly of the generated constructor body shows a pattern like:

```asm
movsd  (%rax), %xmm0        ; load *d
cvtsd2ss %xmm0, %xmm1       ; convert to float -> xmm1
movss  %xmm1, -0x38(%rbp)   ; store to local temp
mov    %r9d, (%rdx)          ; BUG: stores r9d (uninitialized) instead of the float
```

The root cause is in the member-store codegen: after a `double→float` conversion
expression inside a constructor with a reference-typed parameter, the member write
incorrectly uses the 5th integer argument register rather than reloading the float
result from its stack slot or XMM register.

**Workaround**: store the cast result in a local variable first, then assign the
member from that variable.

```cpp
FloatTarget(const double& d) {
    float tmp = static_cast<float>(d);
    value = tmp; // works correctly
}
```

Observed while writing the `test_ctor_const_ref_param_conv_ret0.cpp` regression test.
