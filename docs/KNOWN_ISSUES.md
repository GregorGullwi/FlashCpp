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

## `dynamic_cast` from template static-member helper calls can crash at runtime

While adding regression coverage for cast traversal in delayed static-member
rebinding, a template static-member body that performed `dynamic_cast` on the
result of another same-class static helper compiled and linked, but the produced
program crashed at runtime when the helper returned the address of a local
static object:

```cpp
struct Base {
    virtual ~Base() {}
};

template <typename T>
struct Box {
    static Base* helperBasePtr() {
        static Base value;
        return &value;
    }

    static int value() {
        return dynamic_cast<Base*>(helperBasePtr()) ? 42 : 0;
    }
};
```

Using a `nullptr` return from the helper avoids the crash, which suggests the
remaining bug is in RTTI / `dynamic_cast` lowering or runtime support rather
than in the static-member rebinding change itself.

## ~~Nested template static members of struct type can misbehave at runtime~~ (FIXED)

**Fixed**: struct-typed static members inside nested template classes now work
correctly.  The fix addressed two root causes:

1. `generateTrivialDefaultConstructors()` did not set `current_struct_name_`,
   so unqualified static member references (e.g., `payload.a`) in default
   member initializers could not resolve to the struct's own static members.
2. `resolveGlobalOrStaticBinding()` used the template pattern's store name
   (e.g., `Outer::Inner::payload`) instead of the instantiated name
   (`Outer$hash::Inner::payload`), reading from the wrong global.

Covered by `test_template_nested_static_struct_member_ret45.cpp`.

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

## Delayed static member function bodies can mis-handle same-class member template calls

Explicit template arguments on rebuilt `FunctionCallNode`s are now preserved for
delayed static member function bodies, but there is still a nearby unresolved
bug for same-class member template helper calls in that path. While adding
regression coverage for delayed-body rebinding, the following pattern returned
the wrong result instead of calling the class's own member template:

```cpp
template <typename T>
struct Box {
    template <typename U>
    static int helper() { return int(sizeof(T) + sizeof(U)) + 40; }

    static int value() { return helper<int>(); }
};
```

This indicates that delayed static-member-body rebinding / later resolution for
class-scoped member template calls is still incomplete, even though equivalent
non-template helper calls and non-member template calls are now covered.

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

## `function_signature` propagation from unsubstituted orig type is a no-op for template parameters

Several code paths in `Parser_Templates_Inst_Deduction.cpp` copy `function_signature`
from the *original unsubstituted* template declaration type (`orig_return_type` /
`orig_param_type`) onto the newly constructed substituted `TypeSpecifierNode`. When the
original type IS a template parameter placeholder (e.g., `T` stored as `UserDefined`),
`has_function_signature()` returns false, making the propagation a no-op. The concrete
`function_signature` lives on the resolved `TemplateTypeArg` (in `template_args` /
`explicit_types` / `arg_types`), which is never consulted in these paths.

**Affected locations** (all in `src/Parser_Templates_Inst_Deduction.cpp`):

1. **`try_instantiate_template_explicit`**, return type (~line 700–702):
   copies from `orig_return_type` — should use `findTemplateArgByName` or
   `explicit_types[i]` as the source.

2. **`try_instantiate_template_explicit`**, parameter type (~line 745–747):
   copies from `orig_param_type` — same issue.

3. **`try_instantiate_single_template`** (non-auto path), parameter type (~line 1970–1972):
   copies from `orig_param_type` — should source from the matched `template_args[i]`.

4. **`try_instantiate_single_template`** (fallback return path), return type (~line 1619–1621):
   copies from `orig_return_type` — same issue.

**Correct reference implementation**: the `auto` parameter path at ~line 1911–1914
correctly sources `function_signature` from `deduced_arg_type` (the call-site argument
type), demonstrating the right approach.

**Impact**: When a free function template has a parameter or return type that is a
template parameter substituted with a function-pointer type (e.g.,
`template<typename F> void call(F fn)` instantiated with `int(*)(int)`), the Itanium
mangler may crash with "FunctionPointer type missing function signature". This is the
same class of bug that was fixed for lazy member instantiation and class-template
instantiation in this PR, but the free-function deduction paths were not fully addressed.

**Why it hasn't been observed yet**: The `should_reparse` path (lines 1477–1593) handles
the common case by re-parsing the declaration with template parameters in scope, which
naturally produces a `TypeSpecifierNode` with the correct `function_signature`. The
fallback non-reparse path (lines 1595–1628 for return type, lines 1927–1977 for params)
is only taken when the return type is not template-dependent, which is uncommon for
function-pointer template parameters. Similarly, the explicit-instantiation path
(`try_instantiate_template_explicit`) is less commonly exercised with function-pointer
arguments than the deduction path.

**Suggested fix**: Apply the same `findTemplateArgByName` fallback pattern used in
`Parser_Templates_Lazy.cpp` and `Parser_Templates_Inst_ClassTemplate.cpp`:

```cpp
if (orig_param_type.has_function_signature()) {
    param_type_ref.set_function_signature(orig_param_type.function_signature());
} else if (subst_type_index.category() == TypeCategory::FunctionPointer ||
           subst_type_index.category() == TypeCategory::MemberFunctionPointer) {
    if (const auto* arg = findTemplateArgByName(
            orig_param_type.token().value(), template_params, template_args)) {
        if (arg->function_signature.has_value())
            param_type_ref.set_function_signature(*arg->function_signature);
    }
}
```

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
