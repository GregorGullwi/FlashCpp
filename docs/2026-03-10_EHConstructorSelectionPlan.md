# EH constructor-selection design note

This note documents the constructor-selection slice that is already landed on this branch for exception-handling object materialization.

It also records the final backend-lowering rule that fell out of the debugging: backend address-vs-inline-object decisions must use current-function indirect-stack state, not global/frontend `TempVar` metadata.

## Why this change was needed

The original EH copy/move construction path had two related problems:

- it selected same-type constructors through the explicit-only `findCopyConstructor()` / `findMoveConstructor()` helpers,
- and several by-address lowering paths still depended on fragile pointer/reference metadata handling.

That was enough to miss valid implicit copy/move constructors in EH paths, and it also exposed a broader backend issue where `AddressOfMember`-produced pointer temps could be treated as though they were the address of the spill slot rather than the stored source address.

The practical regression that forced this work was `tests/test_eh_implicit_copy_ctor_ret0.cpp`, but the investigation showed the bug was not EH-only: the same address-handling problem also affected ordinary nested member copy initialization.

## Landed API surface

The central policy helper is now:

- `src/AstNodeTypes_DeclNodes.h`
- `src/AstNodeTypes.cpp`

`StructTypeInfo::findPreferredSameTypeConstructor(bool prefer_move, bool include_implicit = true) const`

Its behavior is:

- for xvalue/prvalue-style same-type sources, prefer a move constructor,
- if move is preferred but unavailable, fall back to copy,
- for lvalue sources, use copy only,
- allow implicit constructors to participate by default,
- respect deleted-copy / deleted-move state before selecting a candidate.

This keeps the same-type constructor policy in one place instead of re-encoding it in EH lowering.

## Where the helper is used

### EH copy / move materialization

`src/IRConverter_Conv_Calls.h` now uses `findPreferredSameTypeConstructor(...)` inside:

- `emitSameTypeCopyOrMoveConstructorCall(...)`

That helper still has an EH-oriented name, but the selection policy it uses is no longer EH-private.

### Generic constructor-call resolution for same-type arguments

The same file also uses the helper while resolving constructor calls whose single argument is a reference/pointer-like same-type source. That keeps ordinary same-type constructor matching aligned with the EH path.

### Implicit copy/move constructor body synthesis

`src/CodeGen_Visitors_Decl.cpp` now consults `findPreferredSameTypeConstructor(...)` when synthesizing implicit copy/move constructor bodies for struct members. If a struct subobject has a same-type constructor path, the generated body emits a nested constructor call instead of falling back to blind member copying.

## Indirect-storage / address-handling piece

The constructor-selection helper alone was not enough. The bug only fully disappeared once by-address lowering stopped confusing:

- true reference-like storage, and
- address-only pointer temps such as `AddressOfMember` results.

The important current behavior is:

- EH tempvar source materialization in `emitSameTypeCopyOrMoveConstructorCall(...)` uses `getIndirectStackInfo(...)` on the **current function's** stack slot instead of relying on stale global `TempVar` metadata,
- generic constructor-call lowering uses `getReferenceInfo(temp, offset)` when deciding whether a by-address argument should load the stored pointer with `MOV` or compute an address with `LEA`,
- generic struct local initialization in `src/IRConverter_Conv_VarDecl.h` also uses current-function indirect-stack state when deciding whether a `TempVar` initializer is inline storage or a pointer to storage,
- the indirect-storage cleanup also routed `AddressOf` and `AddressOfMember` through shared address-only registration helpers so plain pointer temps are no longer implicitly dereferenced by accident.

That split is what makes nested-member copy construction behave correctly again.

## What this change fixes

This landed slice fixes the following class of problems:

- EH throw-side same-type construction can use implicit copy/move constructors,
- catch-by-value construction can use the same same-type constructor policy,
- implicit copy/move constructor synthesis for nested struct members no longer loses observable constructor behavior,
- `AddressOfMember` pointer temps are no longer mis-lowered as if the spill slot itself were the source object,
- caller-side large-struct return-slot initialization no longer misclassifies inline object storage as an indirect pointer source.

## What this change does not attempt

This branch does **not** try to solve all constructor-selection questions at once.

Still intentionally out of scope:

- renaming `emitSameTypeCopyOrMoveConstructorCall(...)` into a fully generic helper,
- a full raw-copy legality audit for all same-type object construction paths,
- broader constructor-overload-resolution redesign.

## Validation and coverage

The key regression for this slice is:

- `tests/test_eh_implicit_copy_ctor_ret0.cpp`

Related nearby coverage that should stay green with this change includes:

- `tests/test_eh_copy_ctor_ret0.cpp`
- `tests/test_eh_move_ctor_ret0.cpp`
- `tests/test_eh_struct_throw_ret0.cpp`
- `tests/test_eh_catch_float_return_ret0.cpp`
- `tests/test_exceptions_catch_funclets_ret0.cpp`
- `tests/test_implicit_copy_ctor_inheritance_ret0.cpp`
- `tests/test_rvo_very_large_struct_ret0.cpp`

In addition, the nested-member path is grounded by the implicit constructor synthesis in `src/CodeGen_Visitors_Decl.cpp`, which now reuses the same same-type constructor selection helper instead of duplicating the old explicit-only policy.

At the time of writing, these changes also pass `./tests/run_all_tests.ps1` on this branch.

## Remaining follow-up worth doing later

Once the separate Windows reference-catch bug work is moved to its own branch, the next cleanup here should be modest and mechanical:

1. rename the EH-named constructor emission helper into a generic same-type construction helper,
2. finish auditing raw-copy fallback legality so the policy is explicit rather than scattered,
3. keep normal declaration/copy-init lowering and EH materialization on the same constructor-selection rules.

That follow-up should remain a cleanup pass so the review surface stays small.