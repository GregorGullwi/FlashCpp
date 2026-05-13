# Template Argument Standard-Conformance Investigation

**Date:** 2026-05-12
**Last updated:** 2026-05-13 (follow-up architecture slices completed)

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

These rules are compatibility constraints while migrating ownership to semantic
lookup, deduction, and instantiation services.

## Completed refactor status

The first-pass refactor described by this investigation has been implemented on
the template standard-compliance branch. The implementation did not attempt to
finish every long-term C++20 item below; it moved the hot paths onto more
standard-shaped ownership boundaries and kept the remaining gaps explicit.

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
   `Traits<T>::template rebind<U>::type`.

The final validation run passed `.\build_flashcpp.bat` and
`pwsh -NoProfile -ExecutionPolicy Bypass -File .\tests\run_all_tests.ps1`:
2352 regular tests compiled, linked, and returned expected values, and all 181
expected-failure tests failed as expected.

The remaining target architecture sections are retained as the next conformance
roadmap, especially full semantic lookup, complete structural NTTP values,
current-instantiation/dependent-base modeling, and constraint
normalization/subsumption.

## What is left / next

The completed refactor should be treated as the foundation, not the finish line.
The immediate cleanup items from the first follow-up pass are closed. The
remaining work is now mostly architecture work plus a few hardening passes that
should be split into focused investigations.

### Open next steps

1. **Unify semantic lookup for template candidate discovery.**
   Template lookup still has direct registry-by-name paths. The next pass should
   route class/function/alias/variable template candidate discovery through one
   semantic lookup result that records declaration identity, lookup kind,
   dependency, and definition-vs-POI timing.

2. **Broaden two-phase lookup records beyond non-dependent function calls.**
   Definition-context records now protect selected non-dependent unqualified
   calls, but qualified names, static initializers, member templates, dependent
   bases, unknown specializations, and broader ADL still need explicit
   definition/POI records.

3. **Implement real structural NTTP values.**
   Typed integral/enum/`nullptr` identity is implemented. Pointer, reference,
   function-pointer, member-pointer, floating-point, and structural class-type
   NTTPs are still unsupported or placeholder categories until parser and
   constexpr evaluation can produce standard semantic values.

4. **Expand shape-only deduction and ranking.**
   Selected overloaded free/member function-template paths can rank
   signature-only candidates before body materialization. The older
   full-instantiation fallback remains for unhandled cases; keep moving
   candidate construction, deduction, constraints, partial ordering, and ranking
   into sema-owned phases.

5. **Complete dependent-name and current-instantiation modeling.**
   `TypeInfo::DependentQualifiedNameRecord` covers high-value member-type
   placeholders, but full `[temp.dep]` support still needs current-instantiation
   identity, dependent base lookup, unknown specialization classification,
   expression qualified-id records, and alias ordering.

6. **Harden constructor annotation fallback into an invariant.**
   Current valid coverage no longer needs the fallback in tested cases, but
   codegen still intentionally keeps a soft fallback for uncovered/invalid
   paths. Keep widening sema coverage until the fallback can be converted into a
   hard diagnostic or invariant without losing valid code.

### Recently closed follow-up items

1. **Close the remaining typed-NTTP identity holes.**
   `makeEvaluatedDeferredBaseValueArg` can still collapse non-bool integer
   values to `TypeCategory::Int`, and exact-template lookup still has a linear
   fallback for integral mismatches. These should be fixed first because they
   directly affect specialization identity, matching, and mangling. **Completed**
   with expression/static-member type preservation and typed key hashing.

2. **Finish static `constexpr` owner/error cleanup.**
   Namespace-qualified constexpr references should be distinguished from
   missing type/template owners, and invalid non-dependent `static constexpr`
   initializers should hard-fail instead of reaching any zero-value recovery
   path. **Completed** while preserving precise dependent deferral.

3. **Simplify dependent NTTP concretization.**
   Concretization should bind names once, substitute once, and evaluate once.
   Duplicate substitute/evaluate attempts make lookup-order bugs hard to see.
   **Completed** for dependent alias/default NTTP evaluation.

4. **Remove the constructor-annotation safety net for valid code.**
   Widen `SemanticAnalysis.cpp` constructor inference until valid programs no
   longer need the codegen-time overload-resolution fallback. Only then should
   the fallback become a hard diagnostic/invariant. **Completed for current
   valid coverage**; the fallback remains soft for uncovered/invalid paths.

5. **Move alias-size fallback into shared sema-owned querying.**
   The current TypeInfo-size -> TypeSpecifier-size -> struct-size chain works
   but should become one shared helper used by every concrete-size consumer.
   **Completed** with shared alias-aware size query helpers.

### Highest-impact architecture tracks

1. **Two-phase lookup records.**
   Store definition-context lookup for non-dependent names and defer only
   dependent lookup to the point of instantiation. This is the highest-impact
   standard-conformance gap because it affects template bodies, static
   initializers, dependent calls, ADL, and qualified lookup.
   The 2026-05-13 follow-up added a conservative definition-context record for
   non-dependent unqualified function calls. Later point-of-instantiation
   overloads are filtered out, while dependent calls still complete ADL at POI.

2. **Structural NTTP value identity.**
   Replace the integral-centric `int64_t` model with typed template-argument
   equivalence for enums, `nullptr`, pointers/references/member pointers,
   floating-point values, and structural class-type values.
   The 2026-05-13 follow-up introduced typed NTTP identity categories and wired
   equality/hashing/mangling through them for currently supported cases. Pointer,
   reference, function-pointer, member-pointer, floating-point, and structural
   class-type NTTPs remain explicit unsupported/placeholder categories until the
   parser and constant evaluator can produce standard semantic values for them.

3. **Deduction/constraints split before materialization.**
   Continue extracting candidate viability, deduction, constraints, partial
   ordering, and overload ranking so normal selection paths do not instantiate
   function or class bodies incidentally.
   The 2026-05-13 follow-up slice routes overloaded free function templates
   and implicit member function templates through signature-only candidate
   materialization for overload conversion ranking before instantiating the
   selected body.  It deliberately keeps the older full-instantiation fallback
   for cases the shape path cannot rank safely.

4. **First-class dependent-name/current-instantiation model.**
   Replace string-like placeholders with semantic dependent entities for
   current instantiation, unknown specialization, dependent bases, dependent
   qualified names, and dependent template-ids.
   The 2026-05-13 follow-up slice added a narrow
   `TypeInfo::DependentQualifiedNameRecord` for dependent member-type
   placeholders and taught type substitution/member declaration copying to
   prefer that record for `typename T::type` and dependent member-template
   chains such as `Traits<T>::template rebind<U>::type`.  It intentionally
   leaves full current-instantiation alias ordering, expression qualified-id
   records, and dependent base/unknown-specialization lookup classification as
   later `[temp.dep]` work.

## Target architecture

### 1. Authoritative declaration and lookup model

Create one semantic lookup service used by parser, sema, constexpr evaluation,
template instantiation, and codegen-facing normalization.

The lookup result should preserve:

- declaration identity, not only spelling;
- declaring scope and namespace/class context;
- whether the result is a type, value, namespace, template, overload set, or
  dependent result;
- access and using-declaration provenance;
- whether ordinary lookup, qualified lookup, member lookup, or ADL produced it;
- point-of-definition versus point-of-instantiation timing.

Template registries should become indexes over declarations, not independent
name lookup mechanisms. `TemplateRegistry` can still cache templates and
instantiations, but candidate discovery should start from semantic lookup.

### 2. First-class dependent name model

Replace string-like dependent placeholders with semantic dependent entities.

The compiler should distinguish:

- dependent type name;
- dependent value expression;
- dependent template name;
- member of current instantiation;
- member of unknown specialization;
- dependent qualified name;
- dependent base lookup;
- dependent template-id;
- unresolved overload set dependent on argument types.

This allows `typename`, `template`, current-instantiation lookup, dependent
bases, and unknown specializations to be implemented according to C++20
`[temp.dep]`, `[temp.res]`, and `[basic.lookup]` instead of by reparsing and
fallback classification.

### 3. Parameter-context-driven template argument classification

Template arguments should be parsed into syntax first, then semantically
classified against the corresponding template parameter.

The target flow should be:

1. parse a template-id into argument syntax nodes without forcing type/value
   classification;
2. perform template name lookup and select the target template declaration or
   overload set;
3. match arguments to parameters, including packs and defaults;
4. classify each argument by parameter kind:
   - type parameter: parse/resolve as type-id;
   - non-type parameter: convert constant expression to the parameter type;
   - template-template parameter: resolve as template-name and match parameter
     list;
5. diagnose mismatches at this layer.

This removes most simple-identifier heuristics from
`parse_explicit_template_arguments(...)`.

### 4. Complete non-type template argument value model

Replace the concrete `int64_t`-centered NTTP identity with a structural
constant-value identity.

The model should represent:

- integral and enumeration values with exact type;
- `bool`, character types, signedness, and width;
- `nullptr`;
- pointer values, including object/function pointers and linkage identity;
- pointer-to-member values;
- references;
- floating-point values using exact representation;
- structural class-type template parameter objects;
- dependent constant expressions with preserved semantic expression identity.

Equivalence and hashing should implement C++20 template-argument equivalence,
not general expression equality.

### 5. Two-phase lookup and point-of-instantiation state

Template declarations should store a semantic definition context:

- ordinary unqualified lookup results from the definition point where required;
- namespace and class scope;
- using directives/declarations visible at definition;
- template parameter environment;
- current-instantiation identity;
- dependent lookup records to be completed at instantiation.

At instantiation, only dependent lookups should be completed with the
point-of-instantiation context. Non-dependent names should not be rebound to
later declarations.

### 6. Separate deduction from instantiation

Function-template calls, member-template calls, CTAD, and deduction guides
should share a semantic deduction engine.

The engine should produce either:

- a complete template argument list;
- a substitution failure;
- a hard diagnostic for non-SFINAE contexts.

It should not materialize a function body or class body while ranking
candidates. Instantiation should happen only after candidate selection and
constraint checking.

Required subareas:

- all C++20 non-deduced contexts;
- forwarding-reference rules;
- array/function parameter adjustments;
- cv/ref qualification transformations;
- template-template parameter deduction;
- pack deduction and pack consistency;
- overload-set deduction;
- conversion function template deduction;
- partial ordering;
- constraints and subsumption.

### 7. Constraint normalization and subsumption

Current concept checks should evolve into a C++20 constraints subsystem:

- normalize requires-clauses and constrained parameters into atomic
  constraints;
- substitute template arguments into constraints under SFINAE rules;
- compare atomic constraints for subsumption;
- use constraints in overload ordering and partial ordering;
- preserve diagnostics for failed associated constraints.

### 8. Instantiation context and substitution ownership

Substitution should operate on a single `TemplateInstantiationContext` rather
than many parallel vectors and maps.

The context should include:

- selected template declaration;
- parameter-to-argument bindings;
- pack bindings and pack expansion state;
- enclosing template contexts;
- current instantiation;
- point of instantiation;
- failure policy;
- lookup context;
- constexpr/sema context.

`TemplateEnvironment` can be the seed, but it should become declaration- and
scope-aware instead of only name-to-argument aware.

### 9. Class template and member template materialization

Class-template materialization should be split into phases:

1. create the instantiated type identity and current-instantiation context;
2. instantiate base-specifiers and member declarations as declarations;
3. register declarations and injected-class-name;
4. defer function bodies and static data member definitions until required;
5. sema-normalize ODR-used entities before codegen;
6. constant-evaluate only when C++ requires immediate evaluation.

Member function templates should use the same deduction and instantiation
context as free function templates, with an added object parameter and class
current-instantiation context.

### 10. Static members and constexpr evaluation

Static data member support should move away from early parser-driven repair.

The target behavior:

- declarations are instantiated with correct type and linkage identity;
- initializers are stored as semantic AST under the definition context;
- constant evaluation receives the instantiated semantic context;
- function calls inside initializers use the unified call-resolution service;
- ODR-use and immediate-constant contexts trigger materialization at the right
  time;
- normalized bytes are a post-sema/codegen artifact, not the source of truth.

Constructor resolution inside instantiated member functions should evolve from
the current two-tier model (sema annotation preferred, codegen-time fallback
when absent) to fully sema-owned overload resolution. The fallback path should
be narrowed and eventually removed as sema type-inference coverage expands.
Until then, the warning log from a missing annotation is the observable
diagnostic signal for coverage gaps.

## Migration strategy

### Phase 1: Document and instrument current behavior

- Add targeted tests for current known behavior before refactoring.
- Add debug assertions that identify when parser heuristics classify an argument
  without target parameter context.
- Track all direct `TemplateRegistry` lookups that act as semantic lookup.
- Track all substitution paths that use raw parameter-name vectors or positional
  maps instead of `TemplateEnvironment`.

### Phase 2: Introduce semantic template-id nodes

- Preserve template-id syntax without committing to type/value classification.
- Store argument syntax and source locations.
- Let semantic resolution classify arguments after template lookup.
- Keep existing `TemplateTypeArg` lowering as a compatibility output.

### Phase 3: Centralize template declaration lookup

- Route class/function/alias/variable template lookup through one semantic
  lookup result.
- Keep `TemplateRegistry` as a cache/index behind lookup.
- Replace unqualified-plus-qualified duplicate registration with declaration
  identity and lookup context.

### Phase 4: Build the conformance-oriented argument matcher

- Match parameters to argument syntax using the target template declaration.
- Fill defaults using the correct prior-parameter context.
- Expand packs only after parameter matching decides pack ownership.
- Produce `TemplateTypeArg` only after semantic classification.

### Phase 5: Replace NTTP identity

- Introduce a structural constant value representation.
- Adapt instantiation keys and mangling to consume it.
- Keep `int64_t` as a temporary carrier only for legacy integral cases.
- Add tests for pointers, references, member pointers, `nullptr`, enums,
  floating-point NTTPs, and structural class NTTPs as support lands.

### Phase 6: Implement two-phase lookup records

- Store definition-context lookup results for non-dependent names.
- Store dependent lookup records for names to complete at instantiation.
- Update expression, type, alias, and static-member initializer substitution to
  consume those records.

### Phase 7: Deduction engine extraction

- Extract deduction from parser instantiation functions into a semantic service.
- Make it produce candidate viability and complete template argument lists
  without materializing definitions.
- Use the same engine for free functions, member functions, constructors, CTAD,
  deduction guides, and conversion functions.

### Phase 8: Constraint subsystem

- Normalize constraints.
- Substitute constraints during candidate viability.
- Implement subsumption for overload and partial ordering.
- Replace current concept checks with this subsystem.

### Phase 9: Materialization fixpoint

- Make sema own all first materialization for ODR-used template entities.
- Run a pre-codegen materialization/normalization fixpoint.
- Convert codegen fallback paths into invariant checks once sema coverage is
  complete.

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
- Pointer/reference/member-pointer NTTPs.
- Floating-point NTTPs.
- Structural class NTTPs.
- Dependent NTTP expressions involving `sizeof`, `alignof`, `noexcept`, and
  static data members.

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

Start with parameter-context-driven template argument classification. It is the
smallest structural change that directly reduces the largest class of current
non-conformance: deciding whether an argument is a type, value, or template
without first knowing the target parameter.

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
   Typed integral/enum/`nullptr` identities are implemented. Add standard
   semantic values and equivalence for pointer, reference, function-pointer,
   member-pointer, floating-point, and structural class-type NTTPs, then migrate
   keys and mangling for those categories.

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
   Extend the current typed scalar identity into value variants and
   equivalence/hashing rules for pointer, reference, function-pointer,
   member-pointer, floating-point, and structural class-type NTTPs.

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
   **Status: partial.** Typed NTTP identities and definition-vs-POI function
   lookup are implemented for supported cases. Pointer/reference/member-pointer,
   floating-point, structural class NTTP values, full semantic lookup, and full
   dependent-base/current-instantiation behavior remain open.

### Targeted TODOs closed by follow-up work

1. **TODO: Preserve exact integral NTTP category during deferred evaluation**
   Never collapse evaluated integral NTTPs to a generic `int` category in
   deferred-base/dependent evaluation paths. Keep original signedness/width/type
   category so specialization identity, matching, and mangling remain C++20
   correct. Enum functional casts already carry `TypeCategory::Enum` at parse
   time and through overload resolution; the remaining gap is in
   `makeEvaluatedDeferredBaseValueArg`, which can collapse any non-bool integer
   NTTP to `TypeCategory::Int`. Apply the same TypeInfo-backed category lookup
   that parser and overload-resolution layers now use. **Closed.**

2. **TODO: Remove linear specialization fallback for integral mismatches**
   Replace O(N) "scan all specializations" fallback behavior in exact template
   lookup with canonical argument normalization that preserves type identity and
   allows stable hash/key lookup. This keeps behavior deterministic and avoids
   lookup policy that can diverge from proper template-argument equivalence.
   **Closed.**

3. **TODO: Treat namespace-qualified names as valid owners in constexpr paths**
   In static-member constexpr normalization/evaluation, distinguish namespace
   owners from missing type/template owners. Namespace-qualified references must
   participate in normal lookup/evaluation instead of being classified as
   missing-owner deferrals. **Closed.**

4. **TODO: Reject invalid `static constexpr` initializers instead of zero-fallback**
   Remove broad recoverable fallback that can zero-initialize non-constant
   `static constexpr` members. Only defer where a precise dependent/missing-owner
   condition is proven; otherwise produce the required hard diagnostic.
   **Closed.**

5. **TODO: Simplify dependent NTTP concretization to a single evaluation pass**
   Ensure dependent-argument concretization does not perform duplicate
   substitute/evaluate cycles that can diverge or hide lookup-order bugs.
   Prefer deterministic order: name binding first, then one expression
   substitution/evaluation attempt. **Closed.**

6. **TODO: Eliminate sema constructor annotation fallback for well-formed code**
   The current soft fallback (sema omits annotation → codegen-time overload
   resolution with warning) is intentional while sema type-inference coverage is
   incomplete. Extend `inferExpressionType` and
   `tryAnnotateConstructorCallArgConversions` coverage in `SemanticAnalysis.cpp`
   until the warning log is never emitted for valid C++20 code. Once coverage is
   complete, convert the fallback to a hard `CompileError`. **Closed for current
   valid coverage; fallback intentionally remains soft for uncovered/invalid
   paths.**

7. **TODO: Consolidate alias size resolution into a shared sema helper**
   The TypeInfo-size → TypeSpecifier-size → struct-size fallback chain in
   `resolveCodegenSizeBits` should be extracted into a shared size-query service
   used by all codegen paths that need a concrete byte/bit count for an
   alias-resolved type, avoiding duplication across independent codegen call
   sites. **Closed.**

### Exit criteria

- template argument kind is no longer decided by parser heuristics for ambiguous
  identifiers;
- static-member constexpr reads use one sema service (no duplicated ad-hoc
  recursive evaluators in IR paths);
- selected deduction and overload-ordering paths run without incidental body
  instantiation;
- two-phase lookup behavior is test-covered for non-dependent function calls vs
  dependent POI/ADL calls;
- NTTP equivalence is type-accurate for supported integral/enum/`nullptr` cases,
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
- NTTP identity/equivalence coverage for enum, pointer/reference/member-pointer,
  `nullptr`, floating-point, and structural class-type arguments;
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
