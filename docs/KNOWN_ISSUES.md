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

## Static member initializers can lose nested helper calls under `.` / `[]`

Template static member initializers still mishandle some nested forms where an
unqualified same-class static helper call sits underneath member access or array
subscript. The helper call should bind to the instantiated class member, but in
practice the initializer can silently fold to zero instead of evaluating the
helper result:

```cpp
template <typename T>
struct Box {
    struct Payload { int value; };
    static constexpr Payload payload = { int(sizeof(T)) + 38 };
    static constexpr const Payload& helper() { return payload; }

    static constexpr int value = helper().value; // expected 42 for T=int
};
```

Likewise:

```cpp
template <typename T>
struct Box {
    static constexpr int values[2] = { 40, int(sizeof(T)) + 38 };
    static constexpr const int* helper() { return values; }

    static constexpr int value = helper()[1]; // expected 42 for T=int
};
```

When reproduced locally, the instantiated storage for `payload`/`values` was
correct, but the synthesized `value` variable still emitted as zero. This
suggests the remaining problem is deeper than AST child traversal alone: the
constexpr/static-initializer path is still not preserving or resolving the
nested helper call correctly once wrapped in `MemberAccessNode` or
`ArraySubscriptNode`.

## Static member initializers can fold nested constexpr member calls to zero

Template static member initializers can still compile/link but evaluate
incorrectly when a same-class constexpr helper call is immediately followed by a
constexpr member-function call on the temporary result:

```cpp
template <typename T>
struct Box {
    struct Holder {
        int value;
        constexpr int get() const { return value; }
    };

    static constexpr Holder make_holder() {
        return Holder{int(sizeof(T)) + 38};
    }

    static constexpr int value = make_holder().get(); // expected 42 for T=int
};
```

When reproduced locally while refactoring static-member initializer AST
traversal, FlashCpp compiled and linked this case but the produced program
returned `0`. This points to a remaining constexpr/static-initializer issue in
the nested `MemberFunctionCallNode` path rather than in the visitor refactor.

## Delayed static member bodies inside `try` statements can still call pattern owners

Delayed parsing of template static member function bodies still has a remaining
resolution bug when the helper call lives inside a `try` statement. A body like:

```cpp
template <typename T>
struct Box {
    static int helper() { return int(sizeof(T)) + 38; }

    static int value() {
        try {
            return helper();
        } catch (...) {
            return 0;
        }
    }
};
```

can still lower the instantiated `Box<int>::value()` body with a relocation to
`Box::helper()` (the pattern owner) instead of `Box$hash::helper()`, leading to
link failures such as `undefined reference to 'Box::helper()'`.

The AST traversal/rebinding helpers now recurse through `TryStatementNode`, but
there is still a later owner-resolution gap in this path.

## Copying a same-type `dynamic_cast` result into another local pointer can drop the value

While trying to add focused regression coverage for recent Windows pointer/local
declaration fixes, the following simpler pattern compiled and linked but still
returned the wrong result at runtime:

```cpp
struct Base {
    virtual int get() { return 42; }
    virtual ~Base() {}
};

int main() {
    Base base;
    Base* source = &base;
    Base* rebound = dynamic_cast<Base*>(source);
    Base* copied = rebound;
    return copied ? 0 : 1; // FlashCpp returns 1
}
```

The existing same-type cast regression (`test_dynamic_cast_debug_ret10.cpp`)
still passes, so the remaining issue appears to be in local-pointer
initialization/copy from the `dynamic_cast` result rather than in the RTTI
classification of the cast itself.

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

## Static-member rebinding drops rebound arguments for non-implicit-this member calls

In `rebindStaticMemberInitializerFunctionCalls`, when a `MemberFunctionCallNode` is
encountered whose object is **not** implicit `this`, the function recursively rebinds
the call's arguments but then discards them — the original unmodified `node` is
returned instead of a reconstructed node carrying the rebound arguments. This means
that static-member function calls nested inside the arguments of such a call are
never redirected to the instantiated class's declarations.

```cpp
template <typename T>
struct Box {
    static T helper() { return T{}; }

    static int compute() {
        Box other;
        // `other.method(helper())` — helper() in the arguments is a
        // FunctionCallNode that should be rebound to Box<int>::helper(),
        // but the MemberFunctionCallNode falls through and returns the
        // original node with un-rebound arguments.
        return other.method(helper());
    }
};
```

For the bug to manifest all three conditions must hold:
1. A `MemberFunctionCallNode` where the object is not `this`
2. The arguments contain calls to static member functions of the same template class
3. Those inner static calls need rebinding (reference uninstantiated declarations)

This is a pre-existing issue — both the old `rebindDelayedStaticMemberFunctionCalls`
and `rebindStaticMemberInitializerFunctionCalls` had the same fall-through. The fix
would be to reconstruct the `MemberFunctionCallNode` with `rebound_args` (and a
recursed object expression) when `!is_implicit_this_call`, instead of returning the
original node.

## Variadic template parameters before non-variadic ones misalign param-arg pairing

In `populateTemplateParamSubstitutions` (`src/Parser_Templates_Inst_Deduction.cpp:160-162`),
variadic template parameters are skipped with `continue`. Because the loop iterates params
and args in parallel by index `i`, skipping a variadic param also skips `template_args[i]`.
This is correct when variadic params appear at the end of the parameter list (the common
case) and pack expansion is handled separately via `pack_param_info_`. However, if a
variadic parameter appears before non-variadic ones (unusual but valid in C++20 with
constrained params or partial deduction guides), the remaining params would be paired with
the wrong args since the `continue` does not adjust the arg-side index.

**Impact**: Only affects templates where a variadic parameter pack precedes a non-variadic
parameter in the template parameter list, which is uncommon in practice. Standard trailing
packs work correctly.

**Suggested fix**: Instead of a parallel-index loop, consume variadic packs separately and
maintain an independent arg cursor, similar to how `try_instantiate_template_explicit`
(same file, line ~390) handles variadic packs with a dedicated `explicit_idx` counter.

## Calling a function-pointer template parameter in a free function template can lower as `operator()`

While validating the free-function `function_signature` propagation fix, the more direct
runtime shape

```cpp
template <typename F>
int apply(F fn, int x) {
	return fn(x);
}
```

still compiled but linked with `undefined reference to 'operator()'` when instantiated as
`F = int (*)(int)`. The same test passed once it stopped *calling* the function pointer and
only used the parameter for instantiation / comparison, which suggests a separate lowering
gap in the call-expression path after template substitution rather than in template argument
binding or mangling.

This was reproduced with both deduced and explicit free-function template instantiations
while adding `tests/test_funcptr_free_function_template_signature_ret0.cpp`.

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
