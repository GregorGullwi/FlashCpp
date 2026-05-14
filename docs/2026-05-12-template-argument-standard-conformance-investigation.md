# Template Argument Standard-Conformance Investigation

**Date:** 2026-05-12
**Last updated:** 2026-05-14 (semantic lookup unification complete)

This document describes how FlashCpp's template argument architecture can move
toward C++20 conformance. It is intentionally architectural: it identifies the
compiler layers that should own each responsibility and the order in which to
separate them.

## Baseline behavior to preserve during migration

The current implementation already depends on several behaviors that must be
preserved through refactoring:

1. expression-lowering fold attempts for static members are scalar-only;
2. folded reads prefer normalized constant bytes when available;
3. recursive base-member static-evaluation paths are bounded (depth limit
   `MAX_RECURSIVE_STATIC_EVAL_DEPTH = 64`);
4. template static-member normalization applies template-parameter substitution
   before expression-level substitution;
5. struct member function codegen takes a per-function IR snapshot before each
   member; on exception only the failing member's partial IR is rolled back
   (`InstructionList::truncateTo`), earlier members are preserved;
6. when sema omits a constructor annotation (`require_sema_resolved_ctor` is
   set but no annotation is present) codegen logs a warning and falls through to
   runtime overload resolution, failing hard only if that also finds no match —
   this fallback must remain until sema coverage is complete;
7. enum functional casts (`Enum(value)` constructor-call syntax) are
   categorized as `TypeCategory::Enum`, not `TypeCategory::Struct`, across
   the parser, overload-resolution conversion matching, and sema type-inference
   layers; `ResolvedQualifiedIdentifierInfo::enum_owner_type_index` provides a
   direct fast path for qualified enum-constant type inference;
8. alias types that resolve to non-struct concrete types participate in codegen
   size resolution through a TypeInfo-size → TypeSpecifier-size → struct-size
   fallback chain.
9. qualified template-body calls preserve definition-context lookup records so
   template-body lookup can distinguish definition-time and point-of-instantiation
   resolution boundaries;
10. floating-point non-type template arguments preserve typed identity through
    constexpr evaluation and mangling.

These rules are compatibility constraints while migrating ownership to semantic
lookup, deduction, and instantiation services.

## Completed refactor status

The first-pass refactor described by this investigation has been implemented on
the template standard-compliance branch. A 2026-05-14 follow-up pass moved more
of the highest-impact template infrastructure onto standard-shaped semantic
ownership boundaries while keeping the remaining gaps explicit.

Completed work:

1. centralized static `constexpr` member reads and recursive base-member
   evaluation policy behind a shared semantic service;
2. preserved ordered, typed NTTP identities in instantiation keys, including
   exact integral/enum identity and typed `nullptr` identity;
3. consolidated alias-aware concrete size queries;
4. added parameter-context-driven classification for explicit template
   arguments across function, class, alias, and variable template use sites;
5. introduced and threaded `TemplateInstantiationContext` through lookup and
   substitution paths;
6. extracted shape-only template deduction viability so selected free/member
   function-template overload paths rank signatures before materializing bodies;
7. fixed regressions found during full-suite validation in dependent alias size
   fallback, template-template leftovers, nested pack/materialization, dependent
   constexpr evaluation, and explicit-template SFINAE deduction;
8. added conservative definition-context lookup records for non-dependent
   template-body calls while preserving dependent POI/ADL completion;
9. added dependent qualified-name records for high-value member-type placeholder
   paths such as `typename T::type` and
   `Traits<T>::template rebind<U>::type`;
10. routed the main function-template, member-template, qualified owner/call,
    variable-template, alias-template, member probe, and operator/ADL template
    lookup paths through semantic lookup requests/records;
11. extended typed NTTP identity beyond integral/enum/`nullptr` to object
    pointers, references, function pointers, and null member pointers;
12. expanded signature-only ranking before body materialization to explicit
    free-function template overloads and fixed explicit overload ranking so
    losing bodies are not materialized;
13. extended dependent qualified-name records to current-instantiation owners,
    unknown-specialization owners, and expression qualified-ids, with stricter
    missing-`typename` diagnostics for covered unknown-specialization paths.
14. preserved definition-context records for qualified non-dependent calls and
    added floating-point NTTP identity flow through constexpr evaluation and
    Itanium mangling.
15. moved remaining low-frequency parser template probes and the static-initializer
    constexpr template probe onto parser-built semantic lookup requests so timing
    follows definition-vs-POI context instead of hardcoded point-of-definition.
16. improved deferred-base-class lookup: `lookup_inherited_template` and
    `lookup_inherited_type_alias` now follow the `type_index` of a deferred base
    (including one level of alias chain) to find a concrete struct and recurse
    into it, instead of always skipping deferred bases entirely.
17. threaded parser context into `getTemplateParametersForTypeInfo` in
    `ConstExprEvaluator_Members.cpp`; when a parser is available the function
    uses `makeTemplateNameLookupRequest` (which propagates POI timing and
    definition-namespace context) instead of the registry convenience wrappers
    that always use `PointOfDefinition` timing.
18. documented why `ExpressionSubstitutor.cpp` existence checks keep direct
    registry calls (simple name-presence check at substitution time, no
    instantiation-context timing needed).

The latest validation run passed the Linux sharded build and all 2351 regular
tests plus 183 expected-fail tests.

The remaining target architecture sections are retained as the next conformance
roadmap, especially deeper dependent-base and segment-chain modeling, ADL
for dependent calls, and constraint normalization/subsumption.

## What is left / next

The completed refactor should be treated as the foundation, not the finish line.
The immediate semantic lookup unification work is now closed. The remaining
work is mostly architecture work that should be split into focused investigations.

### Open next steps

1. **ADL-sensitive dependent completion and deeper two-phase lookup records.**
   All non-dependent unqualified/qualified/member-template/operator paths now
   use semantic lookup requests. The remaining gap is ADL completion for
   dependent argument types and recording definition-context lookup for calls
   from inside template bodies that reach hidden friends or dependent ADL
   namespaces.

2. **Broaden two-phase lookup records further.**
   Definition-context and semantic lookup records now protect selected
   non-dependent unqualified, qualified, member-template, and operator paths.
   Static initializers, richer dependent bases, deeper member-template segment
   chains, and broader ADL still need explicit definition/POI records.

3. **Implement remaining structural NTTP values.**
   Typed integral/enum/`nullptr`, object-pointer, reference, function-pointer,
   and null member-pointer identity is implemented. Non-null member pointers,
   floating-point, and structural class-type NTTPs still need standard semantic
   values, and function/member-pointer mangling still uses a conservative
   fallback for unsupported encodings.

4. **Expand shape-only deduction and ranking.**
   Selected overloaded free, explicit free, and member function-template paths
   can rank signature-only candidates before body materialization. The older
   full-instantiation fallback remains for unhandled cases; keep moving
   candidate construction, deduction, constraints, partial ordering, and ranking
   into sema-owned phases.

5. **Complete dependent-name and current-instantiation modeling.**
   `TypeInfo::DependentQualifiedNameRecord` covers high-value member-type
   placeholders, current-instantiation owners, unknown-specialization owners, and
   expression qualified-id records for practical paths. Full `[temp.dep]` support
   still needs richer dependent base lookup, deeper member-template segment
   chains, injected-class-name handling, and alias ordering.

6. **Harden constructor annotation fallback into an invariant.**
   Current valid coverage no longer needs the fallback in tested cases, but
   codegen still intentionally keeps a soft fallback for uncovered/invalid
   paths. Keep widening sema coverage until the fallback can be converted into a
   hard diagnostic or invariant without losing valid code.

7. **Replace `std::unique_ptr` with a raw pointer with an entry created from a gChunkedVector for `template_arguments` in
   `ExpressionSubstitutor.cpp`.**
   Each segment of a dependent member chain creates a `std::make_shared<std::vector<TemplateTypeArg>>`
   at the substitution call site (`ExpressionSubstitutor.cpp`, around the
   `member_chain.push_back` loop). Since we will keep the pointer the whole lifetime of the. ompiler, the allocation could be sped up by using a ChunkedVector backed pointer, switching
   to a raw pointer (or storing the vector by value if the `DependentMemberAccess`
   ownership model permits) would reduce the two heap allocations per segment
   to one and improve cache locality on the hot template substitution path.
   This requires auditing all consumers of the `template_arguments` shared_ptr
   field on `DependentMemberAccess` (or equivalent) to confirm none alias the
   pointer after the vector is moved.


### Highest-impact architecture tracks

1. **Two-phase lookup records.**
   Store definition-context lookup for non-dependent names and defer only
   dependent lookup to the point of instantiation. This is the highest-impact
   standard-conformance gap because it affects template bodies, static
   initializers, dependent calls, ADL, and qualified lookup.
    The 2026-05-13 follow-up added a conservative definition-context record for
    non-dependent unqualified function calls. The 2026-05-14 follow-up routed the
    main function, member, qualified, variable, alias, probe, and operator
    template lookup paths through semantic lookup requests. Later
    point-of-instantiation overloads are filtered out where records apply, while
    dependent calls still complete ADL at POI.

2. **Structural NTTP value identity.**
   Replace the integral-centric `int64_t` model with typed template-argument
   equivalence for enums, `nullptr`, pointers/references/member pointers,
   floating-point values, and structural class-type values.
    The 2026-05-13 follow-up introduced typed NTTP identity categories and wired
    equality/hashing/mangling through them for currently supported cases. The
    2026-05-14 follow-up added concrete identity flow for object pointers,
    references, function pointers, and null member pointers. Non-null
    member-pointer, floating-point, and structural class-type NTTPs remain
    explicit unsupported/placeholder categories until the parser and constant
    evaluator can produce standard semantic values for them.

3. **Deduction/constraints split before materialization.**
   Continue extracting candidate viability, deduction, constraints, partial
   ordering, and overload ranking so normal selection paths do not instantiate
   function or class bodies incidentally.
    The 2026-05-13 follow-up slice routes overloaded free function templates
    and implicit member function templates through signature-only candidate
    materialization for overload conversion ranking before instantiating the
    selected body. The 2026-05-14 follow-up extended this to explicit
    free-function template overloads and aligned explicit ranking with the
    non-explicit path. It deliberately keeps the older full-instantiation
    fallback for cases the shape path cannot rank safely.

4. **First-class dependent-name/current-instantiation model.**
   Replace string-like placeholders with semantic dependent entities for
   current instantiation, unknown specialization, dependent bases, dependent
   qualified names, and dependent template-ids.
    The 2026-05-13 follow-up slice added a narrow
    `TypeInfo::DependentQualifiedNameRecord` for dependent member-type
    placeholders and taught type substitution/member declaration copying to
    prefer that record for `typename T::type` and dependent member-template
    chains such as `Traits<T>::template rebind<U>::type`. The 2026-05-14
    follow-up added current-instantiation owner identity,
    unknown-specialization owner handling, expression qualified-id records, and
    stricter missing-`typename` diagnostics for covered paths. It intentionally
    leaves richer dependent bases, deeper expression/member-template segment
    chains, injected-class-name handling, and alias ordering as later
    `[temp.dep]` work.

## Target architecture

Keep moving toward one sema-owned template system:

- semantic lookup results own declaration identity, lookup kind, dependency
  state, and definition-vs-POI timing;
- registries act as indexes/caches behind lookup, not independent lookup
  authorities;
- dependent names carry records for current instantiation, unknown
  specialization, dependent bases, dependent template-ids, and dependent
  expression/member chains;
- NTTP identity models C++20 template-argument equivalence for every supported
  category;
- candidate construction, deduction, constraints, overload ranking,
  substitution, and materialization are separate phases;
- static member and constructor handling become sema-owned, with codegen
  fallbacks converted to diagnostics or invariants as coverage expands.

The old phase plan is intentionally removed from this document. The actionable
remaining work is the smaller phased delivery list below.

## Suggested regression coverage

### Template argument classification

- Type parameter versus non-type parameter with the same identifier spelling.
- Qualified type versus qualified static data member.
- Alias template used as type argument and variable template used as value
  argument.
- Dependent `typename T::type` and dependent `T::template rebind<U>`.
- Current-instantiation members and dependent base members.

### NTTPs

- Exact signedness and width distinctions.
- Enum NTTPs.
- `nullptr` NTTPs.
- Object-pointer, reference, function-pointer, and null member-pointer NTTPs.
- Non-null member-pointer NTTPs.
- Floating-point NTTPs.
- Structural class NTTPs.
- Dependent NTTP expressions involving `sizeof`, `alignof`, `noexcept`, and
  static data members. Known failing repro: `Box<sizeof(T)>` and
  `Box<sizeof(T) + 1>` can collapse during instantiation
  identity/materialization.

### Lookup and ADL

- Non-dependent unqualified names unaffected by later declarations.
- Dependent calls that find hidden friends by ADL at instantiation.
- Qualified lookup through dependent base classes.
- Namespace aliases, inline namespaces, using-directives, and using-declarations
  in template definitions and instantiations.

### Deduction

- Non-deduced contexts.
- Forwarding references.
- Overload-set arguments.
- Template-template parameter matching.
- Packs before, between, and after fixed parameters.
- CTAD and user-provided deduction guides.
- Constraint subsumption in overload ordering.

### Static members

- Static constexpr members used as NTTPs inside and outside class templates.
- Static data member initializers that call hidden friends or member templates.
- ODR-use-triggered instantiation timing.
- Dependent initializer failures that should be SFINAE versus hard errors.

## Recommended next step

Start with dependent-path semantic lookup completion and two-phase lookup
records. Lower-frequency parser probes and the static-initializer constexpr
template probe are now on semantic lookup requests; the highest-impact remaining
work is dependent-base lookup, deeper member-template segment chains, and
ADL-sensitive dependent calls using the same declaration-owned lookup records.

## Implementation plan

### Goal

Converge from parser/codegen repair paths to a sema-owned, declaration-context
template system while preserving the newly stabilized static `constexpr`
behavior as compatibility constraints.

### Work plan (remaining phased delivery)

1. **Unify declaration lookup for template names**
   Route class/function/alias/variable template candidate discovery through one
   semantic lookup result so template-id classification and deduction use the
   same declaration identity model.

2. **Advance two-phase lookup records**
   Persist non-dependent lookup at definition time and defer only dependent
   completion to instantiation time. Extend the current function-call record
   slice to qualified names, member templates, static initializer paths,
   dependent bases, unknown specializations, and ADL-sensitive dependent calls.

3. **Consolidate substitution context ownership**
   Continue replacing ad-hoc name/vector substitution channels with
   `TemplateInstantiationContext` as the authoritative owner of scalar
   bindings, pack bindings, current-instantiation identity, lookup context,
   constexpr context, and failure policy.

4. **Separate deduction from instantiation/materialization**
   Expand shape-only candidate viability until deduction, constraints, partial
   ordering, and overload ranking can run without body materialization in normal
   selection paths. Instantiate only after winner selection.

5. **Expand structural NTTP identity beyond supported scalar cases**
   Typed integral/enum/`nullptr`, object-pointer, reference, function-pointer,
   and null member-pointer identities are implemented. Add standard semantic
   values and equivalence for non-null member-pointer, floating-point, and
   structural class-type NTTPs, then migrate keys and mangling for those
   categories. Replace conservative function/member-pointer mangling fallbacks
   where standard entity encodings are available.

6. **Complete dependent-name/current-instantiation modeling**
   Build on `DependentQualifiedNameRecord` with semantic records for expression
   qualified-ids, dependent bases, unknown specialization, injected-class-name,
   and current-instantiation member lookup.

### Concrete artifacts to implement

1. **Semantic lookup result object**
   A single lookup result type carrying declaration identity, lookup kind
   (ordinary/qualified/member/ADL), dependency flags, and definition-vs-POI
   timing metadata.

2. **Template-id syntax node plus semantic classification record**
   Keep parse-time argument syntax unclassified; attach a semantic
   parameter-matching result that records final kind (type/non-type/template),
   conversions, defaults, and pack ownership.

3. **TemplateInstantiationContext**
   Replace ad-hoc maps/vectors with one context object containing scalar
   bindings, pack bindings, current-instantiation identity, lookup context,
   constexpr context, and failure policy.

4. **Structural NTTP value representation**
   Extend the current typed scalar/entity identity into value variants and
   equivalence/hashing rules for non-null member-pointer, floating-point, and
   structural class-type NTTPs. Keep object-pointer, reference, function-pointer,
   and null member-pointer identity in the supported set.

5. **Dependent-name/current-instantiation records**
   Preserve current-instantiation, dependent base, unknown specialization,
   dependent qualified-name, and dependent template-id identity without falling
   back to string-only placeholders.

### Phase gates and dependency order

1. **Gate A (classification)**
   **Status: closed by the first-pass refactor.** Parameter-context
   classification is on by default for explicit template-ids in
   class/function/alias/variable template use sites.

2. **Gate B (lookup unification)**
   **Status: partial, advanced.** `TemplateInstantiationContext` carries lookup
   context and non-dependent function calls now preserve a definition-context
   record, but template candidate discovery is not yet sourced from one
   authoritative semantic lookup result everywhere.

3. **Gate C (instantiation context)**
   **Status: partial.** Core paths now thread `TemplateInstantiationContext`,
   but older substitution and pack expansion paths still reconstruct legacy
   vectors/maps in places.

4. **Gate D (deduction split)**
   **Status: partial, advanced.** Shape-only viability now covers selected
   overloaded free/member function-template paths and should continue expanding
   until all normal overload viability and deduction run without body
   materialization.

5. **Gate E (NTTP and two-phase lookup)**
   **Status: partial, advanced.** Typed NTTP identities and definition-vs-POI
   lookup are implemented for supported cases, including object pointers,
   references, function pointers, and null member pointers. Non-null
   member-pointer, floating-point, and structural class NTTP values remain open,
   along with full semantic lookup coverage and full dependent-base,
   injected-class-name, and current-instantiation behavior.

### Exit criteria

- template argument kind is no longer decided by parser heuristics for ambiguous
  identifiers;
- static-member constexpr reads use one sema service (no duplicated ad-hoc
  recursive evaluators in IR paths);
- selected deduction and overload-ordering paths run without incidental body
  instantiation;
- two-phase lookup behavior is test-covered for non-dependent function calls vs
  dependent POI/ADL calls;
- NTTP equivalence is type-accurate for supported integral/enum/`nullptr`,
  object-pointer, reference, function-pointer, and null member-pointer cases,
  while unsupported structural categories diagnose instead of collapsing to
  scalar identities.

### Minimum regression suite required before declaring completion

- argument classification ambiguities (`X`, `N`, qualified ids, dependent ids)
  resolved by parameter kind, not parser heuristics;
- dependent and non-dependent lookup timing cases that distinguish definition
  context from POI context;
- function-template deduction cases for non-deduced contexts, forwarding refs,
  packs, overload sets, template-template parameters, CTAD, and guides;
- static-member constexpr cases covering scalar gating, normalized-byte reads,
  recursive base-member chains, and depth-limit boundary behavior;
- NTTP identity/equivalence coverage for enum, `nullptr`, object-pointer,
  reference, function-pointer, null member-pointer, non-null member-pointer,
  floating-point, and structural class-type arguments;
- deferred-base NTTP cases proving no category collapse (`unsigned`, `long`,
  `size_t`, fixed-width types) and stable specialization key selection;
- static constexpr initializer diagnostics proving no silent zero-initialization
  for non-constant expressions in non-dependent contexts;
- namespace-qualified constexpr initializer references (`ns::value`) proving
  owner resolution does not misclassify namespaces as missing owners;
- struct with multiple member functions where one fails codegen, verifying that
  the IR rollback preserves the already-emitted earlier members;
- enum functional cast overload resolution, verifying `TypeCategory::Enum` is
  consistently selected over `TypeCategory::Struct` at parser, overload, and
  sema type-inference layers;
- qualified enum constant expressions verifying `enum_owner_type_index` fast
  path produces correct `CanonicalTypeId` without namespace-name-map traversal;
- sema constructor annotation fallback: a ConstructorCallNode form not yet
  covered by sema inference should compile successfully through codegen
  resolution and emit the warning log, not crash.
