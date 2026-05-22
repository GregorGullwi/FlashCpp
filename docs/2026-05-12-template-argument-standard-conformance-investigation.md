# Template Argument Standard-Conformance Investigation

**Date:** 2026-05-12  
**Last updated:** 2026-05-22

This document should stay forward-facing. It is not a historical ledger or
release log. Keep completed work only when it changes what the next refactor
can assume or what the next highest-impact step should be.

## Goal

Move FlashCpp from parser- and registry-owned repair paths toward one
sema-owned template system where:

- dependency classification and definition-vs-POI timing are explicit;
- template arguments are classified against the target parameter;
- dependent names preserve semantic identity;
- substitution, deduction, ranking, and materialization are separate phases;
- replay-heavy paths become invariant-driven instead of fallback-driven.

## Current assumptions

Future work can rely on these being in place:

- semantic lookup records cover the main template lookup paths;
- covered non-dependent template-body lookup preserves definition-context
  binding;
- several replay paths already preserve source positions plus definition lookup
  context instead of relying only on substituted AST state;
- covered owner/member-template chains already reuse a shared owner-aware
  materialization path;
- top-level out-of-line constructor-template replay preserves inner template
  metadata and matches renamed inner template parameters in the covered paths;
- nested out-of-line member-function-template replay preserves instantiated
  outer parameter types while copying definition-side parameter identifiers.

Latest recorded full-suite validation:
`2502` regular tests compiled/linked/runtime-pass, `181` expected-fail tests.

Latest focused regressions added on the current branch:
- `test_template_nested_ool_member_template_outer_param_binding_ret0.cpp`
- `test_template_ool_ctor_template_param_rename_replay_ret0.cpp`

## Remaining work, in priority order

### 1. Finish the next two-phase lookup slice

This is still the highest-impact track.

Next bounded target:

- make replayed out-of-line member-function-template bodies use the same
  owner-correct deferred-base lookup path already used by the covered inline
  class-template-body and constructor-template replay cases.

Why this is next:

- it closes a still-open standards gap in two-phase lookup;
- it removes another AST-only replay fallback instead of adding new repair
  logic;
- it builds directly on the metadata-preservation work that already landed.

Success condition:

- replayed out-of-line member-function-template bodies performing dependent-base
  lookup no longer need special fallback behavior to find the correct member
  template or owner.

### 2. Remove the next replay-metadata gap

After item 1, continue with the remaining declaration/static-member/
deferred-base replay paths that still never captured enough metadata at parse
time.

Rule for this work:

- prefer replay-first semantic attachment;
- do not add new AST-only repair paths unless they are strictly temporary and
  documented.

### 3. Expand dependent-name/current-instantiation modeling only as needed

Still open:

- richer dependent-base records;
- more unknown-specialization coverage;
- deeper member-template/type chains;
- consistent current-instantiation identity across qualified-name paths.

This work should support items 1-2, not replace them as the main track.

### 4. Leave lower-priority tracks for later unless they block 1-3

Still open, but not the next best slice:

- remaining NTTP categories;
- broader sema-owned deduction and ranking;
- final conversion of repair paths into invariants/diagnostics.

## Recommended implementation order

1. add a narrow regression for replayed out-of-line member-function-template
   dependent-base lookup;
2. make that path reuse the same owner-correct replay/lookup machinery as the
   covered inline and constructor-template cases;
3. remove the local fallback that becomes redundant;
4. update these docs with the next remaining replay-metadata gap.

## Regression focus

Keep adding narrow regressions in these areas:

- replayed out-of-line member-function-template bodies using `this->template`
  or equivalent dependent-base lookup;
- declaration/definition attachment where inner and outer template parameters
  interact;
- remaining declaration/static-member replay paths that still fall back to
  partially substituted AST state.

## Exit criteria

This plan is complete when:

- the main replay paths preserve definition-time vs POI lookup timing through
  semantic metadata instead of fallback recovery;
- dependent-name/current-instantiation behavior is semantically modeled rather
  than reconstructed from placeholder spellings;
- remaining repair paths have been converted into diagnostics or invariants;
- ordinary overload selection does not materialize losing candidates by
  default.
