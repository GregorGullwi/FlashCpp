# Template Argument Architecture Audit

**Date:** 2026-05-12  
**Last updated:** 2026-05-18

This document is the short audit for FlashCpp's template infrastructure. Its
main purpose is to describe what is still structurally wrong, what the next
highest-impact work is, and what "good enough" current behavior already exists
so later refactors do not regress it.

## Executive summary

FlashCpp is no longer in the "basic template support" phase. The main
remaining problem is architectural: template classification, dependent-name
modeling, two-phase lookup, deduction, and instantiation still cross parser,
registry, `TypeInfo`, and late substitution layers instead of being owned by
one semantic pipeline.

The compiler now handles a broad set of practical C++20 template cases, but it
is still not standard-conforming as a whole. The largest remaining conformance
lever is still two-phase lookup and dependent-name ownership, not another broad
parser cleanup.

## Current state in one page

Useful to know before changing anything:

- explicit template arguments are mostly classified by parameter context, not
  only parser heuristics;
- main function/member/qualified/alias/operator template lookup paths already
  use semantic lookup requests/records;
- selected non-dependent calls in templates already preserve definition-context
  lookup records;
- unresolved dependent unqualified calls already preserve POI completion
  metadata through substitution, sema, constexpr, and lazy-static paths;
- several static-member instantiation paths now prefer replay-from-source with
  preserved definition-context lookup rather than AST-only substitution;
- inherited dependent-base member-template alias owners now resolve through the
  shared member-chain concretization path for covered base-specifier/member-chain
  cases such as `Mid<T>::template rebind<int>`;
- typed NTTP identity is implemented for integral, enum, `nullptr`, object
  pointer, reference, function pointer, null member pointer, and floating-point
  cases that already materialize as concrete semantic values;
- selected free/member function-template overload paths already do
  signature-only ranking before body materialization.

Validation at this point passed the Windows sharded build. The focused
follow-up regression for inherited member-template alias lookup now passes, and
the remaining full-suite state is down to one runtime mismatch:
`2427` regular tests compiled/linked, `2426` runtime pass, `1` runtime
regression (`test_type_trait_alias_default_nttp_ret0.cpp`), `182`
expected-fail tests.

## What is still wrong

### 1. Two-phase lookup is still incomplete

This remains the biggest standards gap.

What still needs work:

- remaining current-instantiation and type-alias spellings that do not yet
  reuse the same owner-correct POI/definition-time path;
- remaining eager static-member initializer paths that still need AST-only
  fallback because replay metadata was never captured at parse time;
- richer dependent-base lookup and deeper member-template/member-type chains;
- remaining parser-time ADL-sensitive paths outside the already-covered call
  and static-member flows.

Concrete follow-up already identified by recent work:

- alias-selected owners in default-NTTP/qualified-id substitution must now stay
  attached to the semantic alias result type instead of round-tripping through
  helper owners such as `Conditional$...::value`;
- nested alias-target template arguments still need one semantic
  materialization pass so `IsEmpty<Type>`-style stored template-instantiation
  arguments become `IsEmpty<Concrete>` before deferred-base trait evaluation.

### 2. Dependent names are still represented too loosely

`TypeInfo::DependentQualifiedNameRecord` now covers useful cases, but the full
`[temp.dep]` model is still missing.

Still open:

- deeper expression/member-template segment chains;
- plain dependent bases and more unknown-specialization paths;
- injected-class-name modeling;
- alias ordering and current-instantiation identity across all qualified-name
  paths;
- one canonical equivalence service for dependent unevaluated expressions.

### 3. NTTP support is still incomplete

Supported identity is much better than before, but C++20 coverage is still not
complete.

Still open:

- non-null member-pointer semantic values;
- structural class-type NTTPs;
- any remaining floating-point paths that do not yet round-trip through the
  same semantic identity/mangling flow;
- replacing remaining conservative function/member-pointer mangling fallbacks
  such as `TODO(item-8)` in `NameMangling.h`.

### 4. Deduction, constraints, and overload resolution are still too coupled to instantiation

Some important overload paths now rank candidates without materializing losing
bodies, but this is still partial.

Still open:

- more full-instantiation fallback removal;
- non-deduced contexts and forwarding-reference edge cases;
- packs in more positions;
- template-template parameter matching;
- CTAD and deduction guides;
- constraint normalization/subsumption and cleaner sema-owned candidate
  ranking.

### 5. Static-member materialization is still too repair-oriented

Recent work fixed many regressions by preserving replay metadata and using
definition-context lookup more often, but the model is still not purely sema-
owned.

Still open:

- declarations that never capture replay metadata at parse time;
- remaining eager substitution paths that still need AST-only fallback;
- final cleanup to make invalid non-dependent cases diagnostics/invariants
  instead of repair paths.

### 6. Constructor fallback is still intentionally soft

The codegen-time constructor fallback is no longer needed for current valid
coverage in tested paths, but it still exists for uncovered/invalid cases.

Target end state:

- sema always annotates the valid cases;
- codegen fallback becomes a diagnostic or invariant, not a compatibility path.

## Highest-impact next steps

In priority order:

1. **Finish the next two-phase lookup slice**
   - current-instantiation/type-alias follow-up work after inherited member-template
     alias recovery;
   - complete the remaining alias-owner/default-NTTP path so qualified-id
     substitution, not constexpr repair logic, owns alias-selected static-member
     lookup;
   - remaining replay-metadata gaps for static-member initialization;
   - remaining dependent-base/member-chain paths that still bypass the shared
     semantic lookup model.

2. **Continue dependent-name/current-instantiation modeling**
   - richer dependent-base and unknown-specialization records;
   - deeper member-template chains;
   - canonical dependent-expression equivalence.

3. **Complete the remaining NTTP categories**
   - non-null member-pointers;
   - structural class-type NTTPs;
   - remove remaining mangling fallbacks where standard encodings are possible.

4. **Expand sema-owned deduction/ranking**
   - keep moving ordinary overload selection away from full-instantiation
     fallback and toward candidate construction + deduction + constraints +
     ranking before materialization.

## Short completed-state summary

This section is intentionally compact. It only records the completed work that
materially changes what future refactors can assume.

- semantic analyzer unification on parser-owned template-name lookup is done
  for the previously remaining sema-layer probe paths;
- semantic lookup records cover the main template lookup paths used by parser
  and sema;
- member function template bodies now participate in definition-context lookup;
- unresolved dependent unqualified calls preserve POI completion metadata;
- multiple static-member initialization/copy/update paths now preserve replay
  metadata and definition-context lookup;
- dependent template-template owner concretization is in place for covered
  parser substitution paths;
- inherited dependent-base member-template alias owner lookup is now shared for
  covered base-specifier/member-chain concretization cases;
- alias-template lookup results now expose a canonical semantic owner/result
  name and the main qualified-id substitution paths prefer that semantic owner
  over helper instantiated names.

## Exit criteria for this audit

This audit can eventually be retired when all of the following are true:

- template argument kind is no longer decided by parser heuristics in ambiguous
  cases;
- two-phase lookup is covered for non-dependent definition-time binding and
  dependent POI completion across the main call/member/static paths;
- NTTP equivalence is complete for the supported C++20 categories;
- dependent-name/current-instantiation behavior is owned by semantic records
  instead of string-like placeholders;
- normal overload ranking no longer materializes losing candidates by default.
