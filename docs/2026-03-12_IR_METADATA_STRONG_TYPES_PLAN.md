# IR Metadata Strong Types Plan

**Date**: 2026-03-12  
**Status**: In Progress (Slice 3 complete 2026-03-13)  
**Related**: `docs\2026-03-10_EXPR_RESULT_MIGRATION.md`

## Progress

### Slice 3 (2026-03-13) — Completed

- Migrated the five remaining `int pointer_depth` fields in IR op structs to
  `PointerDepth`:

  | Struct              | Field                 |
  |---------------------|-----------------------|
  | `FunctionParam`     | `pointer_depth`       |
  | `FunctionDeclOp`    | `return_pointer_depth`|
  | `HeapAllocOp`       | `pointer_depth`       |
  | `HeapAllocArrayOp`  | `pointer_depth`       |
  | `PlacementNewOp`    | `pointer_depth`       |

- All fields use `= PointerDepth{}` default member initializer.

- Updated write sites across:
  - `src/CodeGen_Visitors_Decl.cpp` (5 sites)
  - `src/CodeGen_Visitors_TypeInit.cpp` (4 sites)
  - `src/CodeGen_Lambdas.cpp` (4 sites)
  - `src/CodeGen_NewDeleteCast.cpp` (4 sites)

- Read sites in `IRTypes_Instructions.h` and `IRConverter_Conv_VarDecl.h`
  (comparisons, loop bounds, pass-to-int) work unchanged via `operator int()`.

- Build: clean (`make main CXX=clang++`, no warnings)
- Tests: 1457 pass / 35 expected-fail correct (baseline unchanged)

### Slice 2 (2026-03-13) — Completed

- Moved `PointerDepth` definition from `src/IROperandHelpers.h` to
  `src/IRTypes_Core.h` so that both `TypedValue` (in `IRTypes_Ops.h`) and
  `ExprResult` (in `IROperandHelpers.h`) use the same type.

- Added `#include <format>` to `IRTypes_Core.h` and a
  `std::formatter<PointerDepth, char>` specialization (delegates to `int`)
  so that `PointerDepth` values work in `FLASH_LOG_FORMAT` calls.

- Changed `TypedValue::pointer_depth` from `int pointer_depth = 0` to
  `PointerDepth pointer_depth = PointerDepth{}`. The explicit default member
  initializer `= PointerDepth{}` suppresses `-Wmissing-field-initializers` for
  positional aggregate initializations.

- Updated all ~40 `TypedValue`-embedded write sites across:
  - `src/IROperandHelpers.h` (`toTypedValue` helpers)
  - `src/CodeGen_Helpers.cpp`
  - `src/CodeGen_Visitors_Decl.cpp`
  - `src/CodeGen_Visitors_Namespace.cpp`
  - `src/CodeGen_Stmt_Decl.cpp`
  - `src/CodeGen_Call_Direct.cpp`
  - `src/CodeGen_Call_Indirect.cpp`
  - `src/CodeGen_Expr_Conversions.cpp`
  - `src/CodeGen_Expr_Operators.cpp`
  - `src/CodeGen_Expr_Primitives.cpp`
  - `src/CodeGen_Lambdas.cpp`
  - `src/CodeGen_NewDeleteCast.cpp`

  Note: `FunctionParam::pointer_depth`, `HeapAllocOp::pointer_depth`,
  `HeapAllocArrayOp::pointer_depth`, `PlacementNewOp::pointer_depth`, and
  `FunctionDeclOp::return_pointer_depth` were intentionally left as `int`
  — they are separate structs that do not participate in the arg-ordering
  bug class targeted by this plan.

- Build: clean (`make main CXX=clang++`, no warnings)
- Tests: 1457 pass / 35 expected-fail correct (baseline unchanged)

### Slice 1 (2026-03-13) — Completed

- Introduced `PointerDepth` strong wrapper type in `src/IROperandHelpers.h`
  - Explicit single-arg constructor `PointerDepth(int)` prevents bare-integer
    construction at call sites
  - Non-explicit default constructor keeps `ExprResult{}` aggregate init working
  - `operator int() const` provides backward-compatible reads so existing
    comparison and read sites require no churn
  - Full set of comparison operators

- Upgraded `ExprResult::pointer_depth` from `int` to `PointerDepth`

- Changed `makeExprResultImpl` to accept `PointerDepth` for the depth parameter

- Changed `makeExprResult` 5-arg overload to require `PointerDepth`:
  `makeExprResult(type, bits, value, type_index, PointerDepth{n})`
  — any caller that passes a bare int `n` now fails to compile

- Updated all affected call sites:
  - `src/CodeGen_Expr_Primitives.cpp`: `makeIdentifierResult` lambda upgraded
  - `src/CodeGen_Expr_Conversions.cpp`: all 5-arg makeExprResult calls wrapped
  - `src/CodeGen_Call_Direct.cpp`: literal `1` wrapped in `PointerDepth{1}`
  - `src/CodeGen_MemberAccess.cpp`: `makeArrayResult` lambda upgraded
  - `src/CodeGen_NewDeleteCast.cpp`: `target_pointer_depth` wrapped

- Build: clean (`make main CXX=clang++`, no warnings)
- Tests: 1457 pass / 35 expected-fail correct (baseline unchanged)

### Deferred

- `TypeIndex` wrapper: `using TypeIndex = size_t` has ~268 array-index usages
  (`gTypeInfo[type_index]`) and ~40+ `TypeIndex foo = 0` initializations across
  the codebase. A proper wrapper with no implicit size_t conversion would be
  high-churn. Deferred to Slice 4 after a broader callsite audit.

- `SizeInBits` wrapper: large footprint; see plan Step 3.

- User-defined literals: optional; see plan UDL section.

## Problem

The `ExprResult` migration surfaced a broader API-design question:

- `TypeIndex` is still effectively an alias-like scalar in many APIs
- `pointer_depth` is still an `int`
- `size_in_bits` is still an `int`
- helper factories such as `makeExprResult(...)` still accept combinations of
  plain scalar arguments that are easy to misorder

That makes some incorrect calls compile even though they represent different
concepts. The recent `makeExprResult(..., 0, 0, ull)` bug is an example of this
class of failure: the call was syntactically valid, but semantically wrong.

## Goal

Introduce stronger types around IR-facing metadata so that:

- `type_index`, `pointer_depth`, and size values are harder to mix up
- helper APIs become more self-describing
- common bad call patterns stop compiling
- the rollout stays incremental and does not block the main `ExprResult`
  migration

## Non-Goals

- No parser or semantic-type redesign
- No broad rename-only sweep across the entire codebase
- No mandatory user-defined-literal rollout
- No requirement that every existing callsite convert to a new style in one pass
- No coupling of this work to the remaining `ExprResult` compatibility cleanup

## Design Direction

The right scope here is **IR/codegen metadata hardening**, not a repo-wide type
system rewrite.

That means:

- start from `ExprResult`, `TypedValue`, and closely related helper APIs
- harden the small number of bug-prone metadata channels first
- keep the first slice narrow enough that failures are clearly attributable to
  the wrapper rollout itself

## Recommended Types

The first candidates are:

1. `TypeIndex`
   - upgrade from alias-like scalar usage to an explicit wrapper type
   - target use: struct/enum/user-defined metadata identity

2. `PointerDepth`
   - explicit wrapper around pointer-depth metadata
   - target use: pointer/reference-like IR metadata and helper APIs

3. `SizeInBits`
   - explicit wrapper for bit-size values where the backend truly wants bits
   - target use: `ExprResult`, `TypedValue`, low-level IR helpers

4. `SizeInBytes` (optional later)
   - useful only if the codebase genuinely benefits from representing bytes
     independently rather than repeatedly multiplying/dividing by 8
   - should not be added unless there is clear local value

## Important Constraint: Avoid Fake Strong Types

If the wrappers accept broad implicit conversion from raw integers everywhere,
the type-safety gain collapses quickly.

So the preferred direction is:

- allow narrowly useful construction at API boundaries
- avoid making every wrapper freely interchangeable with `int`
- prefer explicit factories/helpers over “accept anything and convert later”

This is especially important for:

- `TypeIndex`
- `PointerDepth`
- distinguishing bits from bytes

## What to Do First

### Step 1: Harden `makeExprResult(...)` without waiting for all wrappers

The most immediate value is still to make `makeExprResult(...)` harder to call
incorrectly.

A good first slice is:

- replace the most error-prone defaulted parameter pattern with explicit
  overloads or clearly named constructors/helpers
- make the “encoded metadata bridge” path visibly distinct from normal
  type-index/pointer-depth construction

This gives immediate bug prevention even before stronger wrapper types are fully
introduced.

### Step 2: Introduce `PointerDepth` and `TypeIndex` first

These have the clearest semantic distinction and the highest bug-prevention
value.

Start in:

- `ExprResult`
- `TypedValue`
- `IROperandHelpers.h`
- small helper APIs that currently accept both values positionally

### Step 3: Re-evaluate whether `SizeInBits` should join the first slice

`SizeInBits` is useful, but it may have a much larger callsite footprint than
`TypeIndex`/`PointerDepth`.

Before adding it broadly, audit:

- how many APIs truly want bits rather than generic “size”
- how many callsites currently work in bytes and convert late
- whether introducing `SizeInBits` first would cause noisy churn without much
  additional safety

If the churn is too high, defer `SizeInBits` to a second slice.

## User-Defined Literals

User-defined literals like `_bits`, `_bytes`, `_ptr`, and `_type` are optional
ergonomic sugar, not a foundational requirement.

Recommendation:

- **do not** make UDLs part of the first implementation slice
- only consider them after wrapper adoption is already working well
- add them only if they improve readability at real callsites rather than
  introducing style churn

This avoids turning a safety-focused refactor into a stylistic one.

## Suggested Rollout Order

1. narrow `makeExprResult(...)` hardening
2. introduce `TypeIndex` / `PointerDepth` wrappers in `ExprResult` and
   `TypedValue`
3. migrate the closest helper APIs and bug-prone callsites
4. evaluate `SizeInBits`
5. only then consider optional UDLs or broader API cleanup

## Validation Strategy

For each slice:

1. compile after each API-shape change
2. run focused codegen tests that exercise:
   - struct-returning expressions
   - member access
   - pointer/reference flows
   - conversions and assignments
3. then run the normal full repository regression workflow for the current
   platform

## Risk Areas

- accidental explosion of callsite churn from over-broad wrapper introduction
- wrappers that still permit too many implicit conversions to provide safety
- interaction with existing serialized/encoded metadata bridges
- APIs that currently use `0` as a generic sentinel for multiple concepts
- bits-vs-bytes confusion if both wrapper types are introduced too early

## Concrete Recommendation

Treat this as a **follow-up hardening track** after the main `ExprResult`
migration, not as another phase inside that migration plan.

The best next step is:

1. harden `makeExprResult(...)`
2. prototype `TypeIndex` and `PointerDepth` as true metadata wrappers in a small
   IR/codegen slice
3. measure churn before deciding whether `SizeInBits` and UDLs are worth adding

That keeps the scope narrow, the safety wins real, and the rollout reviewable.
