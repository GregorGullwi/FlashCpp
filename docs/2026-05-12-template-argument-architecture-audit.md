# Template Argument Architecture Audit

**Date:** 2026-05-12  
**Last updated:** 2026-05-28

This document is not a release log. It should explain what the current template
architecture can assume, what is still structurally wrong, and what the next
highest-value cleanup should be.

## Executive summary

The template system has moved away from broad AST-only replay and toward
owner-aware semantic records plus replay-first attachment. The biggest
remaining gap is no longer general parser cleanup. It is the small set of
template replay and dependent-alias routes that still lose semantic
owner/member-chain identity and later recover it textually.

## What the current design can assume

- covered non-dependent template-body lookup preserves definition-context
  binding
- major out-of-line replay paths now prefer source-member identity plus
  substituted-signature evidence over name/arity scans
- constructor and member-template replay are increasingly owner-aware and
  evidence-driven rather than shape-driven
- dependent owner/member-template chains already have a shared semantic
  materialization route in the covered cases
- explicit member-template calls now carry call-argument information early
  enough to avoid caching obviously wrong overloads
- nested member-template alias materialization now preserves substantially more
  outer owner/member-template metadata through parsing, rebinding, and
  materialization
- dependent alias resolution now re-enters semantic owner/member-chain
  resolution before touching the legacy textual `base::member` path

## Main remaining architectural gap

The remaining problem is not that replay exists. The problem is that a few
paths still fail to preserve enough semantic metadata at parse/materialization
time, so deduction-time resolution still has to recover intent from partial or
recordless state.

The clearest example is the residual textual `base::member` path in
`resolveDependentMemberAlias(...)`. A direct removal probe showed that it is no
longer broad infrastructure; it now only protects a small cluster of legacy
recordless cases:

- `test_member_template_alias_preserves_outer_metadata_ret0.cpp`
- `test_alias_template_nested_member_value_ret42.cpp`
- `test_template_current_instantiation_alias_qualified_deeper_member_ret0.cpp`

That is the next best cleanup target.

## Recommended implementation order

1. Remove the last recordless dependent-alias routes.
   Preserve full semantic owner/member-chain records in the three remaining
   cases above, then delete the textual `base::member` path entirely.

2. Keep replay attachment evidence-driven.
   Continue tightening declaration replay so attachment succeeds because source
   identity and canonical substituted signatures match, not because a fallback
   scan guessed correctly.

3. Extend dependent-name modeling only where it unlocks step 1 or 2.
   Richer current-instantiation and unknown-specialization handling still
   matters, but it should be pulled in to unblock concrete replay/alias gaps,
   not pursued as a detached refactor.

4. Leave lower-priority uplift for later unless it blocks the above.
   This includes broader sema-owned ranking/deduction cleanup and sema-level
   modeling for aggregate-forwarding constructor cases.

## Architectural rules for follow-up work

- prefer semantic records over textual reconstruction
- prefer replay-first source identity over instantiated-member scans
- do not add new broad repair paths for unresolved replay attachment
- if a path cannot preserve enough metadata, document the gap explicitly and
  keep the compatibility surface narrow

## Validation guidance

When changing this area, always rerun:

- the focused regression that motivated the slice
- the three residual textual-path blockers listed above when touching dependent
  alias ownership
- `pwsh tests/run_all_tests.ps1` before closing the slice
