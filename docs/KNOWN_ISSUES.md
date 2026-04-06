# Known Issues

This file tracks currently open issues only. Fixed items are removed once they are
validated.

## Member calls on reference-valued complex receivers remain incomplete

Template or lazy member calls on reference-valued complex receivers such as
`static_cast<Foo&>(x).method()` or `(cond ? a : b).method()` can still fall
back into ordinary direct-call lowering or mis-handle the receiver object.

One current failure mode is:

```text
Phase 1: sema-normalized direct call missing resolved target for '...'
```

`parse_member_postfix` now deduces object types for arbitrary expressions, but
`generateMemberFunctionCallIr` (`src/IrGenerator_Call_Indirect.cpp`) still has
receiver-shape-specific lowering and does not yet handle all reference-valued
complex receivers all the way through member-call codegen.

**Workaround:** Bind the expression result to a named reference first:

```cpp
auto& obj = static_cast<Foo&>(x);
obj.method();
```

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
