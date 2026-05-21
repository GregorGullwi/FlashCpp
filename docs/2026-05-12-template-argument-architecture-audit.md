# Template Argument Architecture Audit

**Date:** 2026-05-12  
**Last updated:** 2026-05-21

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
- alias-selected default-NTTP qualified-id owners now canonicalize through a
  shared owner-materialization primitive instead of local alias/default-NTTP
  recovery logic in expression substitution;
- deferred alias/member-chain materialization now recursively concretizes nested
  alias-target template-instantiation arguments, so stored
  `IsEmpty<Type>`-style args become `IsEmpty<Concrete>` before deferred-base
  trait evaluation;
- current-owner qualified-id substitution now resolves member aliases through the
  same member-side lookup/materialization path used by current-instantiation
  member-type lookup, preserving direct dependent-member-target handling and the
  concrete member-alias cache when a concrete sibling alias must be synthesized;
- typed NTTP identity is implemented for integral, enum, `nullptr`, object
  pointer, reference, function pointer, non-null/null member-data pointer, and
  floating-point cases that already materialize as concrete semantic values;
- default non-type template arguments now materialize with their substituted
  declared parameter type for covered scalar/enum cases, so
  `template<class T, T V = 1>` preserves `T=short` instead of storing `V` as
  `int`;
- namespace and member variable-template initializers now capture initializer
  replay positions and definition lookup contexts, and variable-template
  instantiation prefers replay-first initializer materialization before the
  compatibility AST-substitution fallback;
- in-class static-member declarations now preserve initializer definition
  lookup context alongside replay positions on both AST and instantiated
  static-member carriers, and covered nested/member-template static-member
  instantiation now reuses replay-first substitution instead of AST-only
  substitution;
- out-of-line static-member definitions now preserve the full replay-visible
  template-parameter list, and instantiation rebuilds replay state with
  parameter kinds/non-type categories before replay-first substitution, covering
  deeper owner/member-template chains such as
  `MetaFactory<T>::template Wrap<char>::template Step<N>::value`;
- qualified member variable-template chains now use owner-aware lookup through
  instantiated owners and dependent bases, preserving recovered outer class
  template bindings for cases such as `Derived<T>::template member_v<T>`;
- `this->template` member-function-template calls can now find member templates
  declared in dependent base class templates while parsing the derived class
  template body, because deferred base template names are stored on
  `StructTypeInfo` before the primary `TemplateClassDeclarationNode` is
  registered;
- deferred alias targets now preserve and materialize multi-hop member-template
  suffixes for covered alias and base-specifier flows, so spellings like
  `Traits<T>::template Box<U>::template Rebind<T>` are no longer truncated to a
  single dependent member-template hop;
- member alias-template declarations now preserve deferred member suffixes and
  enclosing class-template bindings for covered non-template intermediate member
  chains such as `Provider<T>::Node::template Apply<U>`;
- template-parameter default arguments now route dependent owner template-ids
  such as `Provider<T>::Node::template Apply<T>::value` through the same
  deferred qualified-member-chain parser used in template bodies, instead of
  falling through to the generic postfix `::` parser and stopping before
  semantic substitution/evaluation;
- lookup-time owner materialization for concrete template instantiations now
  canonicalizes through the shared owner helper before qualified member-chain
  resolution, so ratio-like inherited `::type::value` chains can find inherited
  static members during default-NTTP evaluation without reopening deferred-base
  attachment fallback paths;
- direct member-template lookup on fully qualified instantiated nested owners
  now retries shortened nested-owner spellings, so declarations that materialize
  owners like `Outer<int>::Inner::template Apply<int>` can find member-template
  registrations stored under nested names such as `Inner::Apply`;
- dependent qualified-id and call substitution now share owner-correct prefix
  materialization for covered unknown-specialization and dependent-base chains,
  so general expression/lazy-static paths such as
  `Traits<T>::template Box<T>::type::value`,
  `Traits<T>::template Box<T>::type::get()`, and
  `Derived<T>::template Inner<int>::type::value` stay concrete through the
  intermediate `type` alias instead of stalling after the member-template hop;
- selected free/member function-template overload paths already do
  signature-only ranking before body materialization.

Validation at this point passed the Linux sharded build and ELF test workflow:
`2440` regular tests compiled/linked/runtime-pass, `181` expected-fail tests.
Focused Windows/MSVC validation for this slice passed the new deeper
dependent-chain regressions plus the nearby ratio/dependent-member regressions.
Latest Windows/MSVC full-suite validation after this slice:
`2487` regular tests compiled/linked/runtime-pass, `181` expected-fail tests.
Latest focused Linux validation for the out-of-line static-member replay slice
passed `test_template_out_of_line_static_member_replay_member_template_chain_ret0.cpp`,
`test_template_out_of_line_static_member_two_phase_lookup_ret0.cpp`, and
`test_template_out_of_line_static_member_two_phase_lookup_multi_template_ret0.cpp`
after `make sharded CXX=clang++`.

## What is still wrong

### 1. Two-phase lookup is still incomplete

This remains the biggest standards gap.

What still needs work:

- remaining current-instantiation and type-alias spellings that do not yet
  reuse the same owner-correct POI/definition-time path;
- remaining declaration/static-member paths outside the newly covered in-class
  and nested static-member replay flows that still need AST-only fallback
  because replay metadata was never captured at parse time;
- richer dependent-base lookup and deeper member-template/member-type chains;
- remaining parser-time ADL-sensitive paths outside the already-covered call
  and static-member flows.

Concrete follow-up already identified by recent work:

- the explicit-template unknown-specialization owner-record gap is now narrower:
  replayed deferred qualified-call records keep the instantiated placeholder
  `owner_type`, so declaration-time replay is no longer forced to recover that
  identity from the raw owner spelling alone. Deferred non-call and call-shaped
  qualified expressions now also keep member-template segment arguments such as
  `Traits<T>::template Box<T>::value` and
  `Traits<T>::template Box<T>::get()` instead of dropping the `Box<T>` segment
  at parse time.
- default NTTP declared-type materialization and variable-template initializer
  replay metadata are now covered for the targeted scalar/member-chain cases.
  In-class static members now preserve definition lookup context on the stored
  static-member carriers as well, and covered nested/member-template static-
  member instantiation now reuses replay-first substitution with outer/inner
  template bindings instead of immediate AST-only substitution.
  Out-of-line static-member definitions now preserve the full replay-visible
  template-parameter list as well, and replay rebuilds parameter
  names/kinds/non-type categories before substitution so deeper
  owner/member-template chains stay on the replay-first path instead of falling
  back immediately to AST substitution.
  Qualified member variable-template chains through dependent bases are now
  covered for the focused initializer/constexpr paths, and dependent-base
  `this->template` member-function-template calls are covered for the focused
  inline class-template body paths. Deferred alias targets now keep covered
  multi-hop member-template suffixes in alias declarations and base-specifier
  materialization. Member alias-template chains with covered non-template
  intermediate members and enclosing class-template bindings now preserve enough
  metadata for declarations such as `Provider<T>::Node::template Apply<U>`.
  The parser-side declaration stop for dependent owner template-ids in default
  NTTPs is now covered, including direct member-template and nested alias chains
  ending in `::value`, and the later lookup-time `<ratio>` owner gap is now
  covered by canonical owner materialization before inherited member-chain
  lookup. General expression/lazy-static substitution now also reuses a shared
  owner-correct prefix-chain materialization path for the focused
  unknown-specialization and dependent-base `member-template -> type/alias ->
  value/call` cases. The next concrete follow-up is therefore narrower again:
  extend that same owner-correct replay/materialization path into the remaining
  declaration/deferred-base paths that still never captured enough metadata to
  avoid AST-only fallback, with the next bounded target now the still-uncovered
  out-of-line declaration/static-member replay users beyond the newly covered
  deeper member-template chain shape.

### 2. Dependent names are still represented too loosely

`TypeInfo::DependentQualifiedNameRecord` now covers useful cases, but the full
`[temp.dep]` model is still missing.

Still open:

- broader expression/member-template segment chains outside the covered lazy
  static initializer paths;
- plain dependent bases and more unknown-specialization paths;
- injected-class-name modeling;
- alias ordering and current-instantiation identity across all qualified-name
  paths;
- one canonical equivalence service for dependent unevaluated expressions.

### 3. NTTP support is still incomplete

Supported identity is much better than before, but C++20 coverage is still not
complete.

Still open:

- non-null member-function-pointer semantic values;
- structural class-type NTTPs;
- any remaining floating-point paths that do not yet round-trip through the
  same semantic identity/mangling flow;
- replacing the remaining conservative function/member-pointer mangling
  fallbacks in `TODO(item-8)` in `NameMangling.h`; the type side is richer now,
  but value encoding still intentionally falls back conservatively.

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
       alias recovery, nested alias-target deferred-base materialization, and the
       current-owner member-alias qualified-id cleanup is now complete for the
       deeper covered member-chain cases;
   - lookup-time canonical owner materialization now covers the focused
      ratio-style inherited `::type::value` default-NTTP path without mutating
      deferred-base attachment;
    - general expression/lazy-static substitution now covers the focused
      deeper dependent-base and unknown-specialization
      `member-template -> type/alias -> value/call` chains via a shared
      owner-correct prefix materialization path reused by qualified-id and call
      substitution;
    - dependent-base `this->template` member-function-template calls are covered
      for focused inline class-template body cases, including multilevel
      dependent-base chains;
    - remaining replay-metadata gaps outside the covered static-member and
      variable-template initializer paths, now including the newly covered
      out-of-line static-member replay path for deeper member-template chains;
    - remaining declaration/static-member/deferred-base paths that still bypass
      the shared semantic lookup model and therefore fall back to AST-only
      repair, with the next bounded target now the still-uncovered replay users
      outside the covered in-class/nested/out-of-line static-member metadata
      paths.

2. **Continue dependent-name/current-instantiation modeling**
   - richer dependent-base and unknown-specialization records;
   - broader member-template chains outside the covered current lazy-static
     cases;
   - canonical dependent-expression equivalence.

3. **Complete the remaining NTTP categories**
   - non-null member-function-pointers;
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
  over helper instantiated names;
- default-NTTP qualified-id substitution now reuses a shared canonical
  owner-materialization primitive with parser-side owner lookup materialization
  instead of bespoke alias/type-index probing in `ExpressionSubstitutor`;
- current-owner qualified-id member-alias substitution now reuses
  `resolveBaseClassMemberTypeChain` plus dependent-member materialization helpers
  instead of the last open-coded local alias repair block in
  `ExpressionSubstitutor`;
- deeper current-instantiation/type-alias qualified-id namespace chains now also
  resolve through that shared member-side path, and replayed qualified-ids now
  capture current-instantiation metadata early enough that lazy/static replay no
  longer has to depend on a repair-only fallback for those covered chains;
- member-pointer NTTPs now preserve declaring-class identity through
  constexpr/template-arg storage, substitute as `&Class::member` inside template
  bodies, and distinguish same-name/same-offset members from unrelated classes;
- member-pointer NTTP mangling now carries richer member-pointer type identity
  (including the declaring class when recoverable), while value encoding still
  intentionally falls back conservatively until a full Itanium external-name
  form is wired for those values;
- default NTTP values now use the substituted declared parameter type when
  materialized through the shared default-evaluation paths, preserving identity
  for cases like `template<class T, T V = 1>` instantiated as `T=short`.
- variable-template initializers now store replay metadata and definition lookup
  context for namespace and member variable templates, with replay-first
  instantiation covering definition-time lookup and dependent member-template
  call chains before the older AST-substitution fallback is used.
- static-member declarations now store definition lookup context alongside
  replay positions on both AST and instantiated static-member carriers, and
  covered nested/member-template static-member instantiation reuses that stored
  metadata for replay-first substitution before falling back to AST
  substitution.
- out-of-line static-member definitions now also store the full replay-visible
  template parameter list, and replay rebuilds parameter names/kinds/non-type
  categories before substitution so deeper member-template owner chains can stay
  on the replay-first two-phase lookup path.
- qualified member variable-template chains now resolve through inherited
  owner-aware member variable-template lookup, and member variable-template
  instantiation folds recovered outer class-template bindings into initializer
  replay/substitution identity.
- dependent-base member-function-template lookup now stores deferred base
  template pattern names on `StructTypeInfo`, so inline class-template bodies can
  resolve `this->template member<...>()` through dependent base class templates
  before the derived `TemplateClassDeclarationNode` is registered.
- deferred alias targets now store a member-template segment chain instead of a
  single member hop, and covered alias/base-specifier materialization walks that
  chain through the shared owner-aware member lookup path.
- member alias-template declarations now capture deferred suffix chains and
  register instantiated/nested aliases with recovered outer class-template
  bindings, covering non-template intermediate member chains such as
  `Provider<T>::Node::template Apply<U>`.
- template-parameter default arguments now preserve dependent qualified
  member-template chains ending in static-member lookups, covering focused
  declaration-time shapes such as `Provider<T>::Node::template Apply<T>::value`
  and `Provider<T>::Node::type::value` instead of rejecting them in the postfix
  parser before default evaluation.
- lookup-time concrete owner resolution now routes through
  `resolveCanonicalInstantiatedOwnerForLookup` before inherited member-chain
  resolution, covering ratio-style inherited `::type` then default-NTTP
  `::value` flows without adding a new deferred-base or constexpr fallback.
- dependent qualified-id and qualified-call substitution now share owner-
  correct prefix-chain materialization for the covered general
  expression/lazy-static cases where a member-template hop produces an
  intermediate type/alias before the final static member or member call.

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
