# Template Argument Standard-Conformance Investigation

**Date:** 2026-05-12  
**Last updated:** 2026-05-17

This document is the execution plan for moving FlashCpp's template
infrastructure toward C++20 conformance. It intentionally focuses on remaining
work. Completed work is kept only when it changes what the next refactor can
assume.

## Goal

Converge from parser- and registry-owned repair paths to one sema-owned
template system where:

- declaration lookup owns dependency classification and definition-vs-POI
  timing;
- template arguments are classified against the target parameter, not guessed
  early;
- dependent names preserve semantic identity instead of string placeholders;
- NTTP identity models C++20 equivalence for the supported categories;
- deduction, constraints, ranking, substitution, and materialization are
  separate phases;
- static-member and constructor behavior becomes invariant-driven instead of
  fallback-driven.

## Compatibility constraints to preserve

Refactors must not regress these existing behaviors:

1. scalar-only static-member folding and normalized-byte preference;
2. bounded recursive static-member evaluation;
3. template static-member normalization applies template-parameter
   substitution before expression-level substitution;
4. per-member IR rollback only rolls back the failing member;
5. missing sema constructor annotations still fall back to runtime overload
   resolution until sema coverage is complete;
6. enum functional casts stay consistently categorized as `TypeCategory::Enum`;
7. alias-backed non-struct concrete types still resolve size through the
   existing fallback chain;
8. qualified template-body calls preserve definition-context lookup metadata;
9. floating-point NTTP identity continues to round-trip through constexpr and
   mangling for already-supported cases.

## Current assumptions you can rely on

Future work should assume these are already in place:

- semantic template-name lookup requests/records cover the main parser/sema
  lookup paths;
- selected non-dependent template-body calls already preserve definition-time
  lookup records;
- unresolved dependent unqualified calls already preserve POI completion
  metadata through substitution, sema, constexpr, and lazy-static paths;
- multiple static-member replay/copy/update paths already preserve replay
  metadata and definition-context lookup;
- inherited dependent-base member-template alias owners already resolve through
  the shared member-chain concretization path for covered base-specifier flows;
- typed NTTP identity already exists for integral, enum, `nullptr`, object
  pointer, reference, function pointer, null member pointer, and covered
  floating-point cases;
- selected free/member function-template overload paths already rank
  signatures before materializing bodies.

Latest validation passed the Windows sharded suite: `2427` pass,
`182` expected-fail, `0` regressions.

## Remaining work, in priority order

### 1. Finish the next two-phase lookup slice

This is still the highest-impact track.

Immediate targets:

- current-instantiation and type-alias spellings that still bypass the shared
  owner-correct lookup/materialization path;
- remaining declarations and static-member paths that never captured replay
  metadata at parse time and therefore still require AST-only fallback;
- remaining dependent-base and deeper member-template/member-type chains;
- remaining parser-time ADL-sensitive lookup paths outside the already-covered
  call flows.

Most concrete next subtask:

- make alias materialization for spellings such as
  `typename Derived<T>::template rebind<U>` reuse the inherited-owner
  member-template path instead of preserving placeholder alias copies.

### 2. Complete dependent-name and current-instantiation modeling

`DependentQualifiedNameRecord` is a useful base, but it is not a complete
`[temp.dep]` model.

Still needed:

- richer dependent-base records;
- more unknown-specialization coverage;
- deeper expression/member-template chains;
- injected-class-name handling;
- consistent current-instantiation identity across qualified-name paths;
- alias-ordering correctness.

### 3. Centralize dependent-expression equivalence

One canonical semantic equivalence service is still missing.

That service should be used by:

- NTTP identity;
- substitution/materialization comparisons;
- any future dependent-expression memoization or lookup keys.

Without this, identity tracking and later evaluation can still diverge.

### 4. Finish NTTP conformance

The main unsupported categories remain:

- non-null member pointers;
- structural class-type NTTPs;
- remaining end-to-end floating-point coverage if any paths still rely on local
  normalization instead of semantic value identity.

Also required:

- replace remaining conservative function/member-pointer mangling fallbacks
  where full standard encodings are available.

### 5. Expand deduction/constraint separation before materialization

The current shape-only ranking coverage is useful but partial.

Still needed:

- more overload paths off the full-instantiation fallback;
- non-deduced contexts;
- forwarding references;
- more pack shapes;
- template-template parameter matching;
- CTAD and deduction guides;
- stronger constraint normalization/subsumption in ranking.

### 6. Turn fallback-heavy areas into invariants

Two areas still need endgame cleanup:

- constructor annotation fallback;
- static-member materialization fallbacks that remain only because semantic
  ownership is still incomplete.

The goal is not to add more fallback code. The goal is to delete it as semantic
coverage becomes sufficient.

## Recommended implementation order

1. **Two-phase lookup follow-up**
   - finish current-instantiation/type-alias owner recovery;
   - extend replay metadata capture into remaining declarations;
   - remove the next AST-only fallback path.

2. **Dependent-name/current-instantiation expansion**
   - richer dependent-base and unknown-specialization records;
   - deeper member-template chains;
   - owner-correct alias ordering.

3. **Dependent-expression equivalence service**
   - one reusable semantic identity model for unevaluated dependent
     expressions.

4. **NTTP completion**
   - non-null member pointers;
   - structural class NTTPs;
   - remaining mangling cleanup.

5. **Deduction/constraint separation**
   - continue removing full-instantiation fallback from ordinary overload
     selection.

6. **Invariant cleanup**
   - constructor fallback;
   - remaining lookup/materialization repair paths.

## Short artifact list

These are the main architectural pieces still worth building or finishing:

- a fuller sema-owned current-instantiation/dependent-name model;
- a canonical dependent-expression equivalence service;
- fuller `TemplateInstantiationContext` ownership in legacy substitution paths;
- a complete NTTP value representation for the remaining C++20 categories;
- broader sema-owned candidate construction/deduction/constraint/ranking;
- final removal of repair-oriented fallback paths.

## Regression focus for future slices

When working on the remaining items, keep adding narrow regressions in these
areas:

- argument classification ambiguities driven by parameter kind;
- definition-time vs POI lookup timing;
- dependent-base and current-instantiation member-template/type chains;
- NTTP identity for unsupported or partially supported categories;
- deduction edge cases: non-deduced contexts, forwarding refs, packs,
  template-template params, CTAD, guides, constraints;
- static-member replay/ODR-use timing and hidden-friend cases.

## Done, but only as context

This section is intentionally short. It exists only to stop future work from
re-solving already-solved problems.

- semantic analyzer unification on parser-owned template-name lookup is done
  for the previously remaining sema-layer probe paths;
- selected member function template bodies already apply definition-context
  lookup;
- dependent unqualified POI completion is already first-class metadata, not an
  ad hoc retry path;
- several static-member replay and copy/update paths already preserve metadata;
- inherited dependent-base member-template alias owner recovery is already in
  place for covered base-specifier/member-chain cases.

## Exit criteria

This plan is complete when:

- argument kind is always parameter-context-driven for ambiguous template-ids;
- the main template-body/member/static lookup paths obey definition-vs-POI
  timing with explicit semantic records;
- dependent-name/current-instantiation behavior is semantically modeled rather
  than string-reconstructed;
- NTTP identity is complete for the supported C++20 categories;
- ordinary overload selection does not materialize losing candidates by
  default;
- remaining repair paths have been converted into diagnostics or invariants.
