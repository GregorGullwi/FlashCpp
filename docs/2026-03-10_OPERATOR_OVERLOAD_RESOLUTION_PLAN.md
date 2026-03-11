# Operator Overload Resolution Follow-Up Plan

**Date**: 2026-03-10
**Last updated**: 2026-03-11
**Status**: In progress on follow-up branch

## Current status on fresh `origin/main`

After rebasing this branch onto `origin/main`, the previously reported tests all pass:

- `problem_statement_example.cpp`
- `spaceship_default.cpp`
- `test_abbrev_tmpl_trailing_decltype_ret0.cpp`
- `test_constexpr_lambda_returned_closure_ret0.cpp`
- all listed `test_spaceship_*` cases

That means there is no remaining red test in this PR to fix on top of the rebased tree. This document originally captured the larger operator-overload follow-up work that should happen in a separate PR; the work has now started and the sections below track completed and remaining items.

## Progress update on this branch

Completed commits so far:

- `32d05fe0` — detect ambiguous operator-overload candidates
- `001de182` — require explicit RHS types for operator lookup
- `b87594cc` — cache resolved binary operators on the AST

Tests added during this follow-up work:

- `tests/test_operator_ambiguity_mixed_free_fail.cpp`
- `tests/test_operator_ambiguity_mixed_member_free_fail.cpp`
- `tests/test_operator_member_tiebreak_ret0.cpp`
- `tests/test_operator_assign_primitive_ranking_ret0.cpp`

Validation rerun on this branch:

- `./build_flashcpp.bat`
- `./tests/run_all_tests.ps1 test_operator_assign_primitive_ranking_ret0.cpp`
- `./tests/run_all_tests.ps1 test_operator_member_tiebreak_ret0.cpp`
- `./tests/run_all_tests.ps1 test_operator_ambiguity_mixed_free_fail.cpp`
- `./tests/run_all_tests.ps1 test_operator_ambiguity_mixed_member_free_fail.cpp`

## Problem summary

Binary operator overload resolution is in a better state than when this document was opened, but
it is still not fully semantic-first:

- Ambiguity handling and candidate ranking are now substantially aligned with normal overload
  resolution.
- Non-dependent binary expressions can now cache a resolved operator overload on the AST.
- However, operator matching still relies on reduced `(TypeIndex, Type)` summaries in places where
  full `TypeSpecifierNode` semantics would be more correct.
- The AST still cannot explicitly represent a semantic `no_match` result for binary operators.
- Codegen still contains fallback operator lookup for cache-miss cases instead of consuming a fully
  authoritative semantic result.

## Status against the original plan

### Phase 1 — Make operator results structurally correct `[done]`

- `OperatorOverloadResult` now distinguishes `has_match`, `no_match`, and `is_ambiguous`.
- Operator-overload callers now diagnose ambiguity instead of silently picking one candidate.
- This closes the original representability gap versus `OverloadResolutionResult`.

### Phase 2 — Unify ranking behavior `[done]`

- Binary operator ranking now computes the set of undominated viable candidates instead of keeping
  only a running `best`.
- Mixed incomparable candidates now produce an ambiguity result.
- The member-vs-free-function preference is preserved only as a tie-break when candidates are
  otherwise equivalent.

### Phase 3 — Remove `Type::Void` fallback semantics `[mostly done]`

- The default `right_type = Type::Void` has been removed from both binary helpers.
- Callers now pass explicit RHS base-type information.
- The assignment path now threads the actual RHS type and no longer collapses to “first primitive
  overload wins”.
- Remaining gap: operator matching still relies on `(TypeIndex, Type)` summaries rather than full
  `TypeSpecifierNode`-based comparison for references, cv-qualification, arrays, and value
  category.

### Phase 4 — Move ownership toward semantic resolution `[partially done]`

- `BinaryOperatorNode` now caches resolved member/free-function overload choices and ambiguity.
- Parser-side construction records non-dependent, concrete binary overload decisions on the AST.
- Template/substitution copy sites preserve that semantic payload.
- Codegen consumes the cached semantic result first and only falls back to lookup when the AST has
  no recorded decision.
- Remaining gap: the AST still has no explicit `no_match` / `resolved-but-unavailable` state, so
  codegen fallback is still required on cache misses.

## Remaining work

### 1. Finish semantic ownership of non-dependent binary resolution

1. Add an explicit semantic state for `BinaryOperatorNode`, e.g. unresolved / no-match /
   ambiguous / member-match / free-function-match.
2. Record `no_match` in semantic analysis so codegen can trust the AST instead of re-running
   lookup on cache misses.
3. Restrict codegen fallback to template-dependent or otherwise intentionally deferred cases.

### 2. Upgrade operator argument matching beyond `(TypeIndex, Type)`

1. Reuse the same `TypeSpecifierNode`-level comparison logic used by normal overload resolution.
2. Cover reference binding, cv-qualification, array decay/shape, and value category.
3. Re-check self-referential and template-substituted operator parameters against the richer
   matcher.

### 3. Tighten parser/codegen diagnostic parity

1. Make parser-time diagnostics and codegen-time diagnostics agree for the same non-dependent
   source.
2. Ensure ambiguity and no-match diagnostics come from semantic analysis whenever possible.

### 4. Expand regression coverage

Add targeted tests for the still-open cases:

- template/self-referential operator parameters
- reference-qualified and cv-sensitive overload selection
- parser-time diagnostics vs codegen-time diagnostics for the same source
- deferred template-dependent expressions that become resolvable after substitution

## Suggested next steps

1. Introduce an explicit semantic result enum/state on `BinaryOperatorNode`, including `no_match`.
2. Teach parser/semantic analysis to write that full state for every non-dependent binary
   expression.
3. Narrow codegen fallback so it only runs for dependent expressions or intentionally unresolved
   cases.
4. Replace the remaining `(TypeIndex, Type)` matching shortcuts with `TypeSpecifierNode`-aware
   comparison.
5. Add the missing regression tests for reference binding, template/self-reference, and
   parser-vs-codegen diagnostic parity.

## Validation already performed

On the rebased branch, the originally reported failing test list was rerun with
`./tests/run_all_tests.ps1 <test>.cpp -Jobs 1`, and every listed test passed.

On top of that baseline, the follow-up operator-overload commits listed above were rebuilt and the
targeted regression suite was rerun successfully.