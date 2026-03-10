# Operator Overload Resolution Follow-Up Plan

**Date**: 2026-03-10
**Status**: Deferred to follow-up PR

## Current status on fresh `origin/main`

After rebasing this branch onto `origin/main`, the previously reported tests all pass:

- `problem_statement_example.cpp`
- `spaceship_default.cpp`
- `test_abbrev_tmpl_trailing_decltype_ret0.cpp`
- `test_constexpr_lambda_returned_closure_ret0.cpp`
- all listed `test_spaceship_*` cases

That means there is no remaining red test in this PR to fix on top of the rebased tree. This document captures the larger operator-overload follow-up work that should happen in a separate PR.

## Problem summary

Binary operator overload resolution currently lives in a split state:

- General function overload resolution already supports `no_match` vs `ambiguous` vs `match`
  via `OverloadResolutionResult` in `src/OverloadResolution.h`.
- Binary operator helpers still use a separate `OperatorOverloadResult` that can only express
  `has_overload` vs `no_overload`.
- `findBinaryOperatorOverloadWithFreeFunction(...)` ranks candidates with partial operand type
  information and does not explicitly report ambiguity.
- Both binary operator helpers still expose `Type right_type = Type::Void`, which is a fragile
  API contract for primitive-parameter ranking.

## Concrete issues to address

### 1. Ambiguity is not representable in `OperatorOverloadResult`

Current state:

- `src/OverloadResolution.h:794-809` defines `OperatorOverloadResult`
- it has `member_overload`, `free_function_overload`, `has_overload`, and `is_free_function`
- it has no `is_ambiguous` state

This differs from `OverloadResolutionResult` in `src/OverloadResolution.h:438-454`, which can
explicitly return `ambiguous()`.

### 2. Candidate ranking does not diagnose mixed incomparable candidates

Current state:

- `src/OverloadResolution.h:1103-1125` compares each operator candidate only against the current
  `best`
- candidates that are better on one operand and worse on another are skipped
- the helper returns the last surviving `best` instead of proving it is uniquely best

This should be reworked to follow the same “count best matches / detect ties” pattern already
used by general overload resolution.

### 3. `Type::Void` default arguments are a latent footgun

Current state:

- `findBinaryOperatorOverload(...)` at `src/OverloadResolution.h:850`
- `findBinaryOperatorOverloadWithFreeFunction(...)` at `src/OverloadResolution.h:951`

Both helpers default `right_type` to `Type::Void`.

That is non-standard as a stand-in for “unknown operand type”, and it can produce incorrect
primitive matching behavior depending on the caller and helper path.

### 4. Parser/codegen split encourages duplicated semantics

Current state:

- parser-side validation uses weaker checks in `src/Parser_Expr_BinaryPrecedence.cpp`
- codegen performs the richer free-function/member operator lookup in
  `src/CodeGen_Expr_Operators.cpp:868-869`

Longer term, non-dependent binary expressions should be resolved once during semantic analysis,
with codegen consuming the recorded result instead of reconstructing it.

## Recommended follow-up PR scope

### Phase 1: Make operator results structurally correct

1. Extend `OperatorOverloadResult` with:
   - `is_ambiguous`
   - `has_match`
   - helper factories mirroring `OverloadResolutionResult`
2. Update all operator-overload callers to diagnose ambiguity instead of silently selecting one
   candidate.

### Phase 2: Unify ranking behavior

1. Refactor binary operator ranking to use the same “uniquely best viable function” logic as
   general overload resolution.
2. Preserve the member-vs-free-function tie-break rule only when the candidates are otherwise
   equivalent under the standard comparison.

### Phase 3: Remove `Type::Void` fallback semantics

1. Remove the default `right_type = Type::Void` from both binary operator helpers.
2. Require callers to pass complete operand type information.
3. Prefer full `TypeSpecifierNode`-based matching for correctness, especially around reference
   binding and value category.

### Phase 4: Move ownership toward semantic resolution

1. Resolve non-dependent binary operator overloads in parser/semantic analysis.
2. Store the selected overload (or ambiguity/no-match state) on the AST.
3. Let codegen consume the semantic result.
4. Keep deferred resolution only for template-dependent expressions.

## Test plan for the follow-up PR

Add targeted tests for:

- ambiguous free-function vs free-function operator overloads
- ambiguous member vs free-function operator overloads
- primitive parameter member operators where a missing RHS base type would otherwise filter a
  valid candidate
- template/self-referential operator parameters
- parser-time diagnostics vs codegen-time diagnostics for the same source

Prefer `_fail.cpp` tests for ambiguity diagnostics and `_retX.cpp` tests for successful ranking.

## Non-goals for this PR

- no broad operator-resolution refactor
- no AST ownership changes for operator resolution
- no code change solely to chase a now-green test on rebased `origin/main`

## Validation already performed

On the rebased branch, the originally reported failing test list was rerun with
`./tests/run_all_tests.ps1 <test>.cpp -Jobs 1`, and every listed test passed.