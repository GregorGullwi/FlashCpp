# Template Argument Standard-Conformance Investigation

**Date:** 2026-05-12  
**Last updated:** 2026-05-22

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
- in-class static-member declarations now preserve replay positions and
  definition lookup contexts on both AST and instantiated static-member
  carriers, and covered nested/member-template static-member instantiation now
  reuses replay-first substitution instead of direct AST-only substitution.
- out-of-line static-member definitions now preserve the full replay-visible
  template-parameter list, and replay rebuilds parameter names/kinds/non-type
  categories before substitution so deeper owner/member-template chains can stay
  on the replay-first path.
- out-of-line class-template member-function definitions now preserve
  definition-context lookup metadata, and instantiation re-enters that context
  before reparsing the saved body so covered non-dependent unqualified lookup in
  those bodies stays bound to the definition point.
- qualified member variable-template chains through instantiated owners and
  dependent bases now recover the canonical member variable-template owner and
  outer class-template bindings for covered constexpr/initializer flows.
- dependent-base `this->template` member-function-template calls in inline
  class-template bodies now keep enough deferred-base owner metadata to find
  member templates declared in base class templates before the derived class
  template declaration is registered.
- deferred alias targets now keep multi-hop member-template suffixes and
  materialize the covered alias/base-specifier cases through the shared
  owner-aware member lookup path instead of storing only one suffix hop.
- member alias-template declarations now capture deferred suffix chains and
  merge recovered outer class-template bindings while materializing covered
  non-template intermediate member chains such as
  `Provider<T>::Node::template Apply<U>`.
- template-parameter default arguments now reuse the deferred qualified-member
  chain parser for dependent owner template-ids, so declaration-time defaults
  like `Provider<T>::Node::template Apply<T>::value` and
  `Provider<T>::Node::type::value` no longer stop in the generic postfix `::`
  path before semantic substitution/evaluation.
- lookup-time owner materialization for concrete template instantiations now
  reuses the shared canonical owner helper before inherited member-chain
  resolution, covering ratio-style inherited `::type::value` lookup during
  default-NTTP evaluation without perturbing deferred-base attachment.
- dependent qualified-id and qualified-call substitution now share owner-
  correct prefix-chain materialization for the focused general
  expression/lazy-static cases where a member-template hop produces an
  intermediate type/alias before a final static value or call, covering shapes
  such as `Traits<T>::template Box<T>::type::value`,
  `Traits<T>::template Box<T>::type::get()`, and
  `Derived<T>::template Inner<int>::type::value`.
- top-level out-of-line constructor templates on class templates now preserve
  inner template-parameter metadata on the registered out-of-line stub, and
  instantiation attaches deferred body/initializer-list replay positions to the
  matching constructor template by unresolved inner-parameter shape before lazy
  materialization.

Latest validation on Linux sharded build:
`2440` regular tests compiled/linked/runtime-pass, `181` expected-fail tests.
Targeted validation for the latest member alias-template non-template
intermediate chain slice passed the focused ELF regressions after
`make sharded CXX=clang++`.
Latest focused Linux validation for the out-of-line static-member replay slice
passed `test_template_out_of_line_static_member_replay_member_template_chain_ret0.cpp`,
`test_template_out_of_line_static_member_two_phase_lookup_ret0.cpp`, and
`test_template_out_of_line_static_member_two_phase_lookup_multi_template_ret0.cpp`
after `make sharded CXX=clang++`.
Focused Windows/MSVC validation for this slice passed the new deeper
dependent-chain regressions plus nearby ratio and dependent-member regressions.
Latest Windows/MSVC full-suite validation after this slice:
`2502` regular tests compiled/linked/runtime-pass, `181` expected-fail tests.
Latest focused Windows/MSVC validation for the out-of-line constructor-template
replay slice passed
`test_template_ool_ctor_template_base_member_init_replay_ret42.cpp`,
`test_template_nested_ool_ctor_template_init_replay_ret42.cpp`,
`test_template_nested_ool_ctor_template_base_member_init_replay_ret42.cpp`, and
`test_template_nested_ool_ctor_template_ref_init_replay_ret42.cpp`.

## Remaining work, in priority order

### 1. Finish the next two-phase lookup slice

This is still the highest-impact track.

Immediate targets:

- current-instantiation and type-alias spellings that still bypass the shared
  owner-correct lookup/materialization path;
- remaining declarations and static-member paths outside the newly covered
  in-class/nested static-member replay flows that never captured replay
  metadata at parse time and therefore still require AST-only fallback;
- remaining dependent-base and deeper member-template/member-type chains;
- remaining parser-time ADL-sensitive lookup paths outside the already-covered
  call flows.

Most concrete next subtask:

- finish the remaining declaration/static-member two-phase lookup work after the
  latest replay-metadata improvements: deferred explicit-template
  qualified-call records now preserve the instantiated placeholder `owner_type`
  instead of dropping it at parse time, and variable-template initializers now
  capture replay metadata for covered namespace/member declarations. Deferred
  non-call qualified expressions preserve deeper member-template segment
  arguments such as `Traits<T>::template Box<T>::value`, and call-shaped
  expressions preserve `Traits<T>::template Box<T>::get()`. Qualified member
  variable-template chains now recover concrete explicit template arguments and
  outer owner bindings for the covered initializer/constexpr paths.
  `this->template` member-function-template calls through dependent bases now
  work for focused inline class-template body cases, including multilevel base
  chains. The newly covered declaration-time parser stop for dependent owner
  template-ids in default NTTPs is now joined by lookup-time canonical owner
  materialization for the focused `<ratio>` inherited `::type::value` path, so
  parsing/substitution/evaluation no longer lose that chain when concrete owner
  lookup runs. Parsing/substitution now progress past the fixed default-NTTP
  declared-type materialization, the lookup-time ratio owner gap,
  materialization, dependent member-template argument paths, covered member
  alias-template target materialization path, alias-template `sizeof...` NTTP
  target arguments, covered variable-template initializer replay paths, covered
  member variable-template chain recovery, covered dependent-base
  `this->template` member-function-template lookup, and the newly covered
  default-NTTP member-chain parser path. In-class static members now also keep
  definition lookup context on stored AST/instantiated static-member carriers,
  and covered nested/member-template static-member instantiation reuses
  replay-first substitution with outer/inner template bindings instead of
  immediate AST-only substitution.
  Out-of-line static-member definitions now likewise preserve the full replay-
  visible template-parameter list, and replay reconstructs names/kinds/non-type
  categories before substitution so deeper member-template owner chains keep the
  same semantic replay path.
  Covered deferred alias member-template suffix chains in declarations and base
  specifiers now work as well, and covered member alias-template declarations
  now preserve non-template intermediate member segments plus enclosing
  class-template bindings for cases like `Provider<T>::Node::template Apply<U>`.
  General expression/lazy-static substitution now also covers the focused
  deeper dependent-base/unknown-specialization
  `member-template -> type/alias -> value/call` chains via a shared owner-
  correct prefix materialization path reused by qualified-id and call
  substitution. The next bounded follow-up is therefore the remaining
  declaration/static-member/deferred-base paths that still never captured enough
  metadata to stay on that semantic path and still require AST-only fallback.
  The next bounded follow-up is therefore the remaining replay users outside the
  covered in-class/nested/out-of-line static-member metadata and out-of-line
  member-function/body metadata paths. Nested out-of-line member-function-
  template replay now also carries definition-time lookup context through
  eager/lazy instantiation stubs, and covered out-of-line constructor replay now
  preserves initializer-list metadata in both nested-template and generic
  out-of-line registration paths. Top-level out-of-line constructor templates on
  class templates now also retain inner template-parameter metadata on the
  registered stub and reattach deferred body/initializer-list replay state to
  the instantiated constructor template before lazy materialization. Priority is
  now broader deferred-base coverage plus the remaining out-of-line member-
  function-template replay combinations that still need the same owner-correct
  replay path.

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
    - treat dependent owner template-ids in template-parameter defaults as
      covered for the focused member-template and nested-alias `::value` cases;
    - treat lookup-time canonical owner materialization as covered for the
      focused ratio-style inherited `::type::value` default-NTTP path;
    - treat general expression/lazy-static deeper dependent-base and
      unknown-specialization `member-template -> type/alias -> value/call`
      chains as covered for the focused qualified-id and call-substitution
      paths;
    - treat top-level out-of-line constructor templates on class templates as
      covered for deferred body/initializer-list replay attachment once the
      registered stub preserves inner template-parameter metadata and
      instantiation matches the constructor template by unresolved inner-
      parameter shape;
    - treat out-of-line static-member replay as covered for the focused deeper
      member-template owner chain cases once replay-visible template parameters
      are preserved and replay reconstructs parameter kinds/non-type categories;
    - extend owner-correct replay/materialization into the remaining
      declaration, static-member, and deferred-base paths outside the covered
      variable-template initializer, in-class/nested/out-of-line static-member
      replay, and default-NTTP parser flows;
    - remove the next AST-only/deferred-base fallback path, with the next bounded
      target now deferred-base replay users plus the remaining out-of-line
      member-function-template replay gaps and broader declaration/static-member
      replay users.

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
  member-template call chains.
- static-member replay metadata now includes definition lookup context on both
  AST and instantiated static-member carriers, covering the focused nested
  member-template static-member replay paths where outer/inner template
  bindings and definition-time ordinary lookup must survive instantiation.
- qualified member variable-template chain recovery now covers direct,
  dependent-owner, and inherited dependent-base cases such as
  `Derived<T>::template member_v<T>` by resolving the owner-aware member
  variable-template name and folding outer bindings into replay/substitution.
- deferred base template pattern names are now stored on `StructTypeInfo`, so
  inline class-template body parsing can resolve `this->template member<...>()`
  through dependent base class templates without waiting for the derived
  `TemplateClassDeclarationNode` to be registered.
- deferred alias target capture now stores a member-template segment chain, and
  covered alias/base-specifier materialization walks each segment through
  owner-aware lookup so `Owner<T>::template Box<U>::template Rebind<T>` is not
  rejected or truncated at parse time.
- dependent owner template-ids in template-parameter defaults now reuse the
  deferred qualified-member parser instead of the generic postfix `::` path, so
  focused declaration-time chains like
  `Provider<T>::Node::template Apply<T>::value` and
  `Provider<T>::Node::type::value` reach substitution/evaluation as semantic
  qualified names instead of stopping at parse time.
- lookup-time concrete owner resolution now routes through
  `resolveCanonicalInstantiatedOwnerForLookup` before inherited member-chain
  resolution, covering ratio-style inherited `::type` followed by default-NTTP
  `::value` use without introducing another fallback path.
- dependent qualified-id and qualified-call substitution now share owner-
  correct prefix-chain materialization for the covered general
  expression/lazy-static chains where a member-template hop feeds an
  intermediate type/alias before the final static member or member call.
- nested out-of-line member-function-template replay now preserves and reuses
  definition-time lookup context in both eager and lazy instantiation stubs, and
  replay re-enters that captured namespace scope (including global namespace)
  before body parsing.
- out-of-line constructor replay metadata capture now consistently preserves
  initializer-list replay positions across nested-template and generic out-of-
  line member registration paths.
- top-level out-of-line constructor templates on class templates now preserve
  inner template-parameter metadata on the registered stub, and the
  instantiator attaches deferred body/initializer-list replay positions to the
  matching constructor template using unresolved inner-parameter shape instead
  of outer-only substituted type identity.

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
