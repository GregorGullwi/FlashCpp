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

## Local variable uses can misbind inside function templates

Local variables declared inside a function template body can later be rejected as
if they were unresolved non-dependent names:

```cpp
struct Box {
    int value;
    Box(int);
};

template<int N>
int f() {
    Box box(N);
    return box.value; // can fail: "'box' was not declared before the template definition"
}
```

Observed while writing a constructor-overload regression: FlashCpp emitted
`error: non-dependent name 'box' was not declared before the template definition
(C++20 [temp.res]/9)` even though `box` is a local declared earlier in the same
template definition.

This appears to be a template-body local-scope/binding bug rather than a real
two-phase lookup violation.

**Workaround**: avoid introducing a named local in the affected template body;
route the value through a helper call or another expression form instead.

## Some converting-constructor cases are still incomplete

The sema-selected non-struct → struct converting-constructor path now covers
variable copy-initialization, free-function arguments, member-call arguments,
and return expressions. The remaining gap is broader converting-constructor
coverage that still goes through older codegen-only paths, especially
struct-source/struct-destination cases:

```cpp
struct Box {
    int value;
    Box(int v) : value(v) {}
};

Box wrap(Box b) { return b; }

int main() {
    Box src{7};
    return wrap(src).value;   // same-type / struct-source cases are still separate
}
```

Reference binding, temporary materialization, and lifetime extension are also
still handled outside this slice.

**Workaround**: spell the construction explicitly when you run into a remaining
struct-source case, or route through a helper that names the desired object
construction directly.

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
