# EH Constructor Selection Reuse Plan (Forward-Looking)

This note is a **separate-PR plan** for tightening exception-handling object materialization without overfitting to one regression. The codebase is moving, so the goal is to define a stable split between **generic constructor-selection semantics** and **EH-local orchestration**, then land the refactor in small, reviewable slices.

## Why this should be a separate PR

- The recent EH fixes were intentionally narrow and are already validated.
- The next step touches behavior that is broader than one EH bug:
  - implicit copy/move constructors
  - raw-copy fallback legality
  - same-type copy vs move constructor selection
  - consistency between throw-side materialization and catch-by-value binding
- The safest approach is to keep the current bugfix commits isolated and do the broader semantic cleanup as a follow-up PR.

## Current state

### EH-local object materialization paths

- `src/IRConverter_Emit_EHSeh.h`
  - throw-side exception object creation on ELF
  - throw-side exception object creation on COFF/MSVC-style EH
  - `materializeCatchObjectFromRax(const CatchBeginOp&)` for Itanium/ELF catch entry materialization
- `src/ObjFileWriter_EH.cpp`
  - Windows handler metadata emission (`dispCatchObj`, catch flags, etc.)

### Current constructor/copy hooks already reused by EH

- `src/IRConverter_Conv_Calls.h`
  - `emitEhCopyOrMoveConstructorCall(...)`
- `src/IRConverter_Conv_VarDecl.h`
  - catch-by-value local binding currently reaches the helper through `op.use_copy_constructor`

### Existing generic compiler functionality we should reuse better

- `src/AstNodeTypes.cpp`
  - `StructTypeInfo::getConstructorsByParameterCount(...)`
  - `StructTypeInfo::findCopyConstructor()`
  - `StructTypeInfo::findMoveConstructor()`
- `src/CodeGen_Stmt_Decl.cpp`
  - generic declaration/copy-initialization behavior and constructor-selection policy
- `src/CodeGen_MemberAccess.cpp`
  - existing trivial/trivially-copyable checks (if EH needs raw-copy gating)

## Main problem to solve

The EH path currently has a constructor-selection helper, but it is still too EH-specific and too narrow:

- it looks up only explicit copy/move constructors through `findCopyConstructor()` / `findMoveConstructor()`
- those helpers currently skip implicit constructors
- EH still has raw-copy fallback logic inline in the throw paths
- the fallback boundary is not clearly expressed as a shared semantic rule

This creates a risk that EH drifts away from normal object initialization semantics.

## Recommended architectural split

### Layer A: Generic special-member selection

This layer should answer:

- for a same-type source object, should initialization use move ctor, copy ctor, or no ctor?
- should implicit constructors participate?
- if no suitable ctor is found, is raw copy even legal?

This layer should be reusable outside EH.

### Layer B: Generic lowering helper for same-type object construction

This layer should answer:

- given a selected constructor and a source object, how do we emit the call?
- how do we materialize `this` and the source argument from a `TypedValue` / stack object / variable name?

This layer can stay in IR/lowering code, but it should not be EH-named if it is shared.

### Layer C: EH-local orchestration

This layer should remain EH-specific:

- `__cxa_allocate_exception` / `__cxa_throw`
- Windows throw record setup / throw-info wiring
- catch funclet / landing-pad orchestration
- ABI-specific catch-object pointer handling

EH should decide **when** an exception object or catch object must be materialized, but should not own the core copy-vs-move-vs-raw-copy policy.

## Proposed refactor boundaries

## Track 1: Extract a shared same-type constructor-selection helper

### Objective

Create one reusable helper that selects the correct special-member constructor for same-type initialization, including implicit constructors.

### Candidate location

- preferred: `StructTypeInfo` helper(s) in `src/AstNodeTypes.cpp` / declaration in `src/AstNodeTypes_DeclNodes.h`
- acceptable alternative: a small semantic utility near codegen/lowering if it needs `TypedValue`-specific policy inputs

### Desired behavior

- for lvalue source:
  - prefer copy constructor
- for xvalue/prvalue source:
  - prefer move constructor
  - fall back to copy constructor when move is unavailable
- include implicit constructors, not just user-written ones
- reject deleted/unusable constructors if the metadata already exposes that state
- return enough information for the caller to decide whether raw-copy fallback is legal

### Notes

`getConstructorsByParameterCount(1, false)` is already closer to what EH needs than the current explicit-only `findCopyConstructor()` / `findMoveConstructor()` pair.

## Track 2: Replace EH-only constructor emission helper with a generic lowering helper

### Objective

Rename/rework `emitEhCopyOrMoveConstructorCall(...)` into a generic same-type construction helper.

### Current file

- `src/IRConverter_Conv_Calls.h`

### Desired outcome

The helper should:

- take a selected constructor policy/result rather than rediscovering policy ad hoc
- emit the constructor call for:
  - destination stack object
  - destination pointer-to-object
- accept source objects that currently appear in these paths:
  - `TempVar`
  - named variable (`StringHandle`)
- stay reusable by:
  - EH throw-side materialization
  - EH catch-by-value local binding
  - any future same-type copy/move-init lowering path

### Non-goal

- do **not** make this helper responsible for ABI-specific EH runtime calls

## Track 3: Unify raw-copy fallback policy

### Objective

Remove open-coded EH fallback decisions where possible and make raw copy contingent on a shared legality rule.

### Desired rule

- if a non-trivial same-type construction path requires a special member, do not silently raw-copy
- raw copy is acceptable only where the type is trivially copyable / otherwise semantically safe according to existing compiler rules

### Candidate reuse

- audit and possibly lift existing triviality helpers from `src/CodeGen_MemberAccess.cpp`
- if those helpers are too codegen-local, create a small shared trait helper instead of duplicating logic in EH

## Track 4: Apply the shared logic across all EH object paths

### Throw-side paths

- `src/IRConverter_Emit_EHSeh.h`
  - ELF exception object materialization
  - COFF exception object materialization

These two paths should share the same:

- special-member selection rule
- move-vs-copy preference
- raw-copy fallback gate

### Catch-side paths

- `materializeCatchObjectFromRax(...)` should stay focused on ABI-specific catch entry storage/pointer handling
- catch-by-value local binding should continue to flow through generic variable/object initialization where possible
- if any catch path currently bypasses generic struct initialization, bring it back under the same shared helper

### Explicitly unchanged EH-local code

- personality/landing-pad dispatch
- funclet setup
- `__cxa_begin_catch` / `__cxa_end_catch`
- handler metadata emission in `src/ObjFileWriter_EH.cpp`

## File-by-file plan

### `src/AstNodeTypes_DeclNodes.h` / `src/AstNodeTypes.cpp`

- add a helper for inclusive special-member discovery (implicit + explicit)
- keep current explicit-only helpers if other callers still rely on that exact behavior
- prefer adding a new API instead of silently changing existing semantics unless all callers are audited

### `src/IRConverter_Conv_Calls.h`

- replace EH-named constructor emission helper with a generic same-type construction helper
- keep source-address materialization logic centralized here rather than duplicating it in EH

### `src/IRConverter_Conv_VarDecl.h`

- route `op.use_copy_constructor` through the shared helper/result
- verify catch-by-value locals still use the same path after the refactor

### `src/IRConverter_Emit_EHSeh.h`

- remove local constructor-selection policy from throw-side paths
- use the shared helper/result in both ELF and COFF emission branches
- keep only EH runtime/orchestration logic in this file

### `src/CodeGen_Stmt_Decl.cpp`

- audit for possible reuse, but keep this PR conservative
- only refactor normal declaration code if the extraction is truly no-behavior-change and reduces duplication immediately

### `src/CodeGen_MemberAccess.cpp`

- audit existing triviality helpers for reuse as the raw-copy legality gate
- if extraction would be noisy, defer that part and document the boundary clearly in the PR

## Behavior matrix the PR should satisfy

- `throw obj;` where `obj` is an lvalue class object
  - copy ctor path
- `throw static_cast<T&&>(obj);`
  - move ctor preferred, copy fallback if needed
- `throw T(...);`
  - move/copy behavior consistent with current value-category policy used by EH
- `catch (T value)`
  - by-value binding uses constructor semantics, not blind raw copy
- `catch (T&)` / `catch (const T&)` / `catch (T&&)`
  - no accidental by-value materialization
- trivial POD-ish types
  - still allowed to use raw-copy paths where legal

## Test plan for the follow-up PR

### EH-focused regressions

- explicit copy ctor on throw + catch by value
- explicit move ctor on xvalue throw
- implicit copy ctor with observable nested-member copy side effect
- implicit move ctor if current parser/type synthesis supports it reliably
- trivial raw-copy struct case
- catch-by-reference / catch-by-rvalue-reference cases near the same area

### Failure-boundary tests

- deleted copy or move constructor, if representable today
- type that should not be raw-copied but currently might be

### Validation commands

- targeted EH tests first
- nearby copy/move construction tests outside EH if touched
- `./build_flashcpp.bat`
- `./tests/run_all_tests.ps1`

## Non-goals

- no redesign of parser EH syntax handling
- no landing-pad/personality redesign
- no Windows metadata format redesign
- no broad constructor-overload-resolution rewrite beyond what EH needs to share safely

## Review strategy

The PR should be staged so the reviewer can answer three questions independently:

1. Is the new shared constructor-selection API correct?
2. Does EH now use that API consistently across throw and catch materialization paths?
3. Are raw-copy fallbacks now limited to semantically safe cases?

## Suggested implementation order

1. Add shared special-member selection API.
2. Add focused implicit-copy EH regression(s) to lock the target behavior.
3. Convert the generic lowering helper in `IRConverter_Conv_Calls.h`.
4. Switch EH throw-side ELF and COFF paths to the shared helper.
5. Re-audit catch-by-value binding paths.
6. Tighten raw-copy fallback gating.
7. Run focused EH regressions, then full suite.

## Exit criteria

This follow-up is done when:

- EH no longer has its own private copy-vs-move constructor-selection policy
- implicit special members participate correctly where EH needs them
- raw-copy fallback is clearly gated by shared legality rules
- both major throw-side backends and catch-by-value binding follow the same semantic policy
- targeted EH regressions and the full test suite pass