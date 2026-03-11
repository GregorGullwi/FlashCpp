# Operator Overload Resolution Follow-Up Plan

**Date**: 2026-03-10
**Last updated**: 2026-03-11
**Status**: Implemented and validated on follow-up branch

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
- `83d0d8ac` — prevent stale member/operator caches from affecting codegen
- `9a433661` — cache ambiguous operator resolution on the AST

Completed follow-up work after those commits:

- Added explicit semantic binary-operator resolution states on `BinaryOperatorNode`
  (`unresolved`, `no_match`, `ambiguous`, `member`, `free-function`)
- Recorded semantic `no_match` results during parser-side non-dependent operator resolution
- Restricted codegen fallback to genuinely unresolved/deferred cases instead of treating
  cache-miss and no-match the same way
- Switched binary operator argument ranking to `TypeSpecifierNode`-aware matching so
  reference binding, cv-qualification, self-referential parameters, and substituted
  user-defined operands are compared with the richer overload-conversion rules
- Fixed SFINAE operator probing and member/free-function tie-break regressions discovered
  while validating the richer matcher

Tests added during this follow-up work:

- `tests/test_operator_ambiguity_mixed_free_fail.cpp`
- `tests/test_operator_ambiguity_mixed_member_free_fail.cpp`
- `tests/test_operator_member_tiebreak_ret0.cpp`
- `tests/test_operator_assign_primitive_ranking_ret0.cpp`
- `tests/test_operator_rhs_ref_qualifier_ret0.cpp`
- `tests/test_operator_free_rhs_cv_ret0.cpp`

Validation rerun on this branch:

- `./build_flashcpp.bat`
- `./tests/run_all_tests.ps1 test_nested_classes_ret16.cpp`
- `./tests/run_all_tests.ps1 test_operator_assign_primitive_ranking_ret0.cpp`
- `./tests/run_all_tests.ps1 test_operator_member_tiebreak_ret0.cpp`
- `./tests/run_all_tests.ps1 test_operator_ambiguity_mixed_free_fail.cpp`
- `./tests/run_all_tests.ps1 test_operator_ambiguity_mixed_member_free_fail.cpp`
- `./tests/run_all_tests.ps1 test_operator_overload_template_ret40.cpp`
- `./tests/run_all_tests.ps1 test_sfinae_operator_plus_ret5.cpp -Jobs 1`
- `./tests/run_all_tests.ps1 test_explicit_ctor_nonmatch_not_blocking_ret42.cpp -Jobs 1`
- `./tests/run_all_tests.ps1 test_operator_rhs_ref_qualifier_ret0.cpp -Jobs 1`
- `./tests/run_all_tests.ps1 test_operator_free_rhs_cv_ret0.cpp -Jobs 1`
- `./tests/run_all_tests.ps1 -Jobs 1`

## Problem summary

Binary operator overload resolution is now semantic-first for non-dependent expressions on this
branch, with the remaining follow-up risk concentrated in diagnostic polish and future
deferred-expression feature work:

- Ambiguity handling and candidate ranking are aligned with normal overload resolution.
- Non-dependent binary expressions now cache resolved member/free-function matches, ambiguity, and
  explicit semantic `no_match` on the AST.
- Parser-time speculative member lookups no longer poison codegen's lazy member cache across the
  parse/codegen boundary.
- Binary operator matching now uses `TypeSpecifierNode`-aware comparison in the operator helpers.
- Codegen consumes the recorded semantic result first and only falls back when the node was left
  intentionally unresolved/deferred.

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

### Phase 3 — Remove `Type::Void` fallback semantics `[done]`

- The default `right_type = Type::Void` has been removed from both binary helpers.
- Callers now pass explicit RHS base-type information.
- The assignment path now threads the actual RHS type and no longer collapses to “first primitive
  overload wins”.
- Binary-operator callers now use the richer `TypeSpecifierNode` matcher where semantic matching
  matters.

### Phase 4 — Move ownership toward semantic resolution `[done]`

- `BinaryOperatorNode` now caches resolved member/free-function overload choices and ambiguity.
- `BinaryOperatorNode` now also stores explicit semantic states for unresolved and no-match cases.
- Parser-side construction records non-dependent, concrete binary overload decisions on the AST.
- Parser-side ambiguity and non-dependent no-match are now cached on the AST instead of being
  rediscovered later.
- Template/substitution and rebinding paths intentionally do **not** preserve cached operator
  resolution payloads when rebuilding `BinaryOperatorNode`s; codegen re-resolves in those cases to
  avoid stale operator pointers after substitution.
- `LazyMemberResolver` is cleared between parsing and codegen so speculative parser-time member
  queries on incomplete classes do not poison later codegen.
- Codegen consumes the cached semantic result first and only falls back to lookup when the AST has
  no recorded decision.

## Follow-up items

### 1. Tighten parser/codegen diagnostic parity

1. Make parser-time diagnostics and codegen-time diagnostics agree for the same non-dependent
   source.
2. Ensure ambiguity and no-match diagnostics come from semantic analysis whenever possible.

### 2. Expand regression coverage

Add or keep adding targeted tests for edge cases that are now supported but worth pinning down:

- template/self-referential operator parameters
- reference-qualified and cv-sensitive overload selection
- parser-time diagnostics vs codegen-time diagnostics for the same source
- deferred template-dependent expressions that become resolvable after substitution

## Suggested next steps

1. Add a dedicated regression for a deferred/template-dependent operator expression that becomes
   resolvable only after substitution.
2. Add an explicit parser-vs-codegen diagnostic parity regression for operator no-match and
   ambiguity.
3. Keep the codegen fallback path narrow and treat any future expansion of it as a smell to audit.

## Validation already performed

On the rebased branch, the originally reported failing test list was rerun with
`./tests/run_all_tests.ps1 <test>.cpp -Jobs 1`, and every listed test passed.

On top of that baseline, the follow-up operator-overload work listed above was rebuilt and the
targeted regression suite plus the full `./tests/run_all_tests.ps1 -Jobs 1` sweep were rerun
successfully.