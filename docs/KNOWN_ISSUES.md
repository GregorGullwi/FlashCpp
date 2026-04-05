# Known Issues

This file tracks currently open issues only. Fixed items are removed once they are
validated.

## Indirect calls through function pointers returning struct types

`emitIndirectCall` (`src/IrGenerator_Call_Indirect.cpp`) computes the return
size via `get_type_size_bits(ret_type)` and wraps it with `nativeTypeIndex(ret_type)`.
Both helpers only handle native/primitive `TypeCategory` values correctly —
`get_type_size_bits` returns `0` for `TypeCategory::Struct` (falls through to
the `default` case in `src/AstNodeTypes.cpp:399-447`), and `nativeTypeIndex`
returns a placeholder `TypeIndex{0, Struct}` instead of the real struct's type
index.

This means that if a function pointer's return type is a struct, the
`ExprResult` produced by `emitIndirectCall` will carry a zero size and an
invalid type index, which may cause incorrect code generation downstream.

The same limitation exists in all pre-existing indirect-call sites that were
consolidated into `emitIndirectCall` (e.g. the `MemberAccessNode` function
pointer paths). It is **not** a regression introduced by PR #1094.

**Affected code:**
- `AstToIr::emitIndirectCall` — `src/IrGenerator_Call_Indirect.cpp:1790-1792`
- `get_type_size_bits` — `src/AstNodeTypes.cpp:399-447`
- `nativeTypeIndex` — `src/AstNodeTypes.cpp:164-170`

**Workaround:** None currently. Function pointers whose return type is a struct
are not yet exercised by the test suite.

## Range-for with inline struct iterator member functions

Range-for loops using struct iterators with inline member function definitions
(operator*, operator++, operator!=) crash at runtime (signal 11). Out-of-line
definitions work correctly. See `tests/test_range_for_auto_struct_iterator_ret0.cpp`
for a working pattern.

## Enum constant value category in sema-owned constructor argument typing

**Severity**: high — causes wrong constructor selection
**Regression test**: `tests/test_ctor_enum_prvalue_ret0.cpp`
**Tracking**: `docs/2026-04-04-codegen-name-lookup-investigation.md` Phase 2, item 6

`inferExpressionValueCategory` in `src/SemanticAnalysis.cpp` must classify
enum constants (enumerators) as `PRValue`, not `LValue`. The current
`IdentifierBinding::EnumConstant` guard is unreliable: when sema marks an
enumerator as an lvalue, `getExpressionType` encodes `ReferenceQualifier::LValueReference`
on the returned type, and constructor overload resolution prefers a
`const T&` overload over a `T&&` overload — even when the `const T&`
overload is deleted.

Reproducer:

```cpp
// Test: enum constants stay prvalues in sema-owned constructor argument typing.
// If sema marks the enumerator as an lvalue, overload resolution picks the
// deleted const-reference overload instead of the rvalue-reference overload.
enum Number {
	Seven = 7
};
struct Sink {
	int value;
	Sink(const Number&) = delete;
	Sink(Number&& number)
		: value((int)number) {}
};
int main() {
	Sink sink(Seven);
	return sink.value - 7;
}
```

Expected return value: `0`
Actual return value: `92`

## Parser: member postfix object type deduction limited to identifier expressions

`parse_member_postfix` (`src/Parser_Expr_PostfixCalls.cpp:205-225`) resolves
`object_struct_name` only when the object expression is a simple
`IdentifierNode`. For complex expressions such as
`static_cast<Foo&>(x).method()`, `(cond ? a : b).method()`, or
`getObj().method()`, the struct name remains unknown (`std::nullopt`).

When the struct name is not deduced the following features are skipped:

- Template member function instantiation (explicit and argument-deduced)
- Lazy member function instantiation
- SFINAE member-existence validation

This is a pre-existing limitation carried over from the original duplicated
`.` / `->` handling in `apply_postfix_operators` / `parse_postfix_expression`
and is **not** a regression introduced by the `parse_member_postfix`
refactoring.

**Workaround:** Assign the complex expression to a named variable before
calling the member function so the parser sees an `IdentifierNode`:

```cpp
auto obj = static_cast<Foo&>(x);
obj.method();  // object type is now deducible
```

**Possible fix:** Use `get_expression_type()` (already available on the
`Parser` class and used by `tryResolveMemberFunctionTemplate` at
`src/Parser_Expr_PostfixCalls.cpp:17`) to deduce the object type for
arbitrary expressions, not just identifiers.

## Return statement implicit constructor materialization uses triple-fallback strategy

`visitReturnStatementNode` (`src/IrGenerator_Visitors_Namespace.cpp:266-329`)
materializes implicit converting constructors when a return expression's type
does not match the function's struct return type (e.g., `return 42;` when the
return type has a converting constructor from `int`). This is correct per
C++ copy-initialization semantics, but the implementation uses three
progressively looser resolution strategies:

1. **Type-based overload resolution** via `resolve_constructor_overload` with
   the argument type inferred by `buildCodegenOverloadResolutionArgType`.
2. **Arity-based resolution** via `resolve_constructor_overload_arity` matching
   single-argument constructors.
3. **Lone-viable-constructor fallback** that manually scans all non-copy/move
   constructors accepting one argument and picks the result only when exactly
   one candidate is found.

Copy and move constructors are correctly excluded via
`isImplicitCopyOrMoveConstructorCandidate`.

**Potential issue:** The triple-fallback could mask ambiguous constructor
overloads that a conforming compiler should reject. If strategy (1) fails
due to missing type information (e.g., a dependent expression whose type
`buildCodegenOverloadResolutionArgType` cannot infer), strategy (2) or (3)
may silently select a constructor that full overload resolution would have
deemed ambiguous. In practice this is unlikely to cause incorrect code for
well-formed programs, but ill-formed programs may be accepted instead of
diagnosed.

**Affected code:**
- `AstToIr::visitReturnStatementNode` — `src/IrGenerator_Visitors_Namespace.cpp:266-329`
- `resolve_constructor_overload` — type-based constructor overload resolution
- `resolve_constructor_overload_arity` — arity-only constructor overload resolution

**Possible fix:** Unify the three strategies into a single overload-resolution
call that always has access to the argument type. When the argument type
cannot be inferred, report an ambiguity diagnostic instead of falling back
to looser matching.
