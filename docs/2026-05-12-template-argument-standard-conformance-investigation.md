# Template Argument Standard-Conformance Investigation

**Date:** 2026-05-12  
**Last updated:** 2026-05-20

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
- deferred alias/member-chain materialization now recursively concretizes nested
  alias-target template-instantiation arguments before deferred-base trait
  evaluation;
- current-owner qualified-id member aliases now reuse the same member-side
  lookup/materialization path as nearby current-instantiation member-type
  resolution, including direct dependent-member-target handling and concrete
  member-alias cache population when a concrete sibling alias must be materialized;
- typed NTTP identity already exists for integral, enum, `nullptr`, object
  pointer, reference, function pointer, non-null/null member-data pointer, and
  covered floating-point cases;
- default NTTP values use the substituted declared parameter type in the shared
  default-evaluation paths for covered scalar/enum cases, so
  `template<class T, T V = 1>` preserves the `T` identity after substitution;
- member alias-template type-specifier parsing now uses the shared dependent
  member-alias materialization path before falling back to direct target
  rebinding, so aliases like
  `typename Select<true>::template Apply<Wrap, int>` can materialize through
  `typename Function<Type>::type` to a concrete type for variable-template
  constexpr checks;
- `sizeof...(Pack)` now parses and preserves as a dependent value expression
  inside alias-template target argument lists, covering MSVC-style
  `index_sequence_for = make_index_sequence<sizeof...(Types)>`;
- selected free/member function-template overload paths already rank
  signatures before materializing bodies.
- namespace and member variable-template initializers now preserve replay
  positions and definition lookup contexts; instantiation uses replay-first
  materialization for covered initializer expressions before falling back to
  stored AST substitution.

Latest validation on Windows sharded build:
`2453` regular tests compiled/linked/runtime-pass, `181` expected-fail tests.

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

- finish the remaining dependent-base/unknown-specialization member-chain work
  after the latest replay-metadata improvements: deferred explicit-template
  qualified-call records now preserve the instantiated placeholder `owner_type`
  instead of dropping it at parse time, and variable-template initializers now
  capture replay metadata for covered namespace/member declarations. Deferred
  non-call qualified expressions preserve deeper member-template segment
  arguments such as `Traits<T>::template Box<T>::value`, and call-shaped
  expressions preserve `Traits<T>::template Box<T>::get()`. The next concrete
  gap is qualified member variable-template chains, where replay can reparse the
  initializer but expression substitution does not always recover concrete
  explicit template arguments, plus declarations outside the covered replay
  flows. The current `<ratio>` blocker remains a useful bounded target because
  parsing and `std::ratio_less` constexpr comparison now progress to later IR
  conversion/link failures rather than stopping in the fixed default NTTP
  declared-type materialization, dependent member-template argument paths,
  covered member alias-template target materialization path, alias-template
  `sizeof...` NTTP target arguments, or covered variable-template initializer
  replay paths.

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

- non-null member-function pointers;
- structural class-type NTTPs;
- remaining end-to-end floating-point coverage if any paths still rely on local
  normalization instead of semantic value identity.

Also required:

- replace the remaining conservative function/member-pointer mangling fallbacks;
  member-pointer type information is richer now, but value encoding still falls
  back conservatively until a full standard external-name form is available
  from current semantic data.

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
   - treat the deeper current-instantiation/type-alias member-chain recovery work
      after the current-owner member-alias qualified-id cleanup as complete for
      the covered qualified-id/member-side cases;
   - keep reusing the shared member-side lookup/materialization path in the
      remaining qualified-id spellings so local repair logic is not reintroduced;
   - extend replay metadata capture into remaining declarations outside the
     covered static-member and variable-template initializer paths;
   - remove the next AST-only fallback path.

2. **Dependent-name/current-instantiation expansion**
   - richer dependent-base and unknown-specialization records;
   - deeper member-template chains;
   - owner-correct alias ordering.

3. **Dependent-expression equivalence service**
   - one reusable semantic identity model for unevaluated dependent
     expressions.

4. **NTTP completion**
   - non-null member-function pointers;
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

- alias-selected default-NTTP qualified-id substitution now canonicalizes the
  substituted owner through a shared parser/substitutor owner-materialization
  primitive instead of bespoke alias/default-NTTP recovery in
  `ExpressionSubstitutor`;

- semantic analyzer unification on parser-owned template-name lookup is done
  for the previously remaining sema-layer probe paths;
- selected member function template bodies already apply definition-context
  lookup;
- dependent unqualified POI completion is already first-class metadata, not an
  ad hoc retry path;
- several static-member replay and copy/update paths already preserve metadata;
- inherited dependent-base member-template alias owner recovery is already in
  place for covered base-specifier/member-chain cases;
- main qualified-id substitution paths now prefer semantic alias owners over
  helper instantiated names when alias materialization already resolved a
  concrete `TypeInfo`;
- current-owner member-alias qualified-id substitution now resolves through
  `resolveBaseClassMemberTypeChain` and dependent-member materialization helpers
  instead of bespoke local alias repair in `ExpressionSubstitutor`;
- deeper current-instantiation/type-alias qualified-id namespace chains now go
  through the same shared member-side path, and replay parsing preserves covered
  current-instantiation qualified-id metadata early enough for lazy/static
  substitution to stay semantic instead of falling back to repair;
- member-data-pointer NTTPs now keep declaring-class identity through
  constexpr/template-arg round-trips and substitute correctly inside template
  bodies; mangling now preserves richer member-pointer type identity but still
  keeps conservative fallback value encoding until a full external-name form is
  available.
- variable-template initializer replay is now available for namespace and member
  variable templates, covering definition-time lookup and dependent
  member-template call chains while leaving qualified member variable-template
  chain recovery as the next focused follow-up.

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
