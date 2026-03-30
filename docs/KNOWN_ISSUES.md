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

## Function-body IR conversion failures can still emit broken object files

When `AstToIr` throws an `InternalError` while lowering a function body,
`FlashCppMain.cpp` currently logs the failure but can continue emitting an object
file for the translation unit instead of stopping compilation with a hard error.
That can turn frontend lowering bugs into misleading runtime mismatches or other
garbage behavior instead of a clean compile failure.

This was observed while reproducing the now-fixed prvalue member-access bug:
`main` logged an IR conversion failure for `Box(7).value`, but the compiler
still produced an object file and the test linked and returned the wrong value.

**Future task**: make function-body IR conversion failures fatal for the current
translation unit, or otherwise suppress object emission when any required
top-level node fails IR generation.

## Virtual reference-return calls can still fail in lowered callers

Minimal virtual calls that return references through a base pointer/reference can
still hit frontend lowering failures in the caller body (observed as
`IR conversion failed for node 'main': bad any_cast`), and because function-body
IR conversion failures are not yet fatal, the compiler may still emit a broken
object file that later crashes at runtime.

This was observed while trying to add focused runtime coverage for virtual
`T&&` / `T&` returns through a base-class call path. Direct non-virtual
reference-return coverage passes; the virtual-reference-return caller path still
needs dedicated investigation.

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

## `wchar_t{}` / `char16_t{}` / `char32_t{}` constructor-call syntax in non-template context

Using `wchar_t{}`, `char16_t{}`, or `char32_t{}` as direct constructor-call expressions
(e.g., `constexpr wchar_t v = wchar_t{};`) fails with a parser error:
"Failed to parse initializer expression".  The keyword-typed wide character types are not
yet handled in the expression parser's constructor-call path.

**Workaround:** Use cast syntax (`static_cast<char16_t>(0)`) or a template helper
(`template<typename T> constexpr T zero_init() { return T{}; }`).

The template path works correctly since `T{}` is parsed as a dependent expression, and
the constexpr evaluator (ConstExprEvaluator_Core.cpp `evaluate_constructor_call`) now
handles `WChar`, `Char8`, `Char16`, and `Char32` in its zero-initialization switch.

## Function pointer stored in a template struct — indirect call IR error

Instantiating a template struct whose member is a typedef'd or `using`-alias function
pointer, then calling through that member, triggers an IR conversion error:

> IR conversion failed for node 'call': Function pointer member missing
> function_signature for indirect call return type

The struct's `StructMember::function_signature` is not populated for function-pointer
members that arrive via template instantiation, so `IrGenerator_Call_Indirect.cpp` throws
an `InternalError` and skips the function body.  The resulting object file has no code for
the function, causing a runtime crash (signal 11) if the caller is linked.

**Workaround:** Use a non-template struct holding the function pointer.

## Implicit function-name → function-pointer conversion for overload resolution

Passing a bare function name to a parameter whose declared type is a typedef'd or
`using`-alias function pointer fails overload resolution:

```cpp
typedef int (*IntFn)(int);
void call(IntFn f);
int foo(int x) { return x; }
call(foo);  // ERROR: No matching function for call to 'call'
```

**Workaround:** Assign the function to a typed local variable first and pass the variable.
