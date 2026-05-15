# Template Argument Architecture Audit

**Date:** 2026-05-12
**Last updated:** 2026-05-15 (dependent unqualified call POI metadata/completion added; lazy static-member replay now restores definition lookup context; ADL function-template POI completion broadened)

This document describes the current FlashCpp template-argument architecture for
types, non-type values, template-template arguments, class templates, function
templates, constructors, member templates, static members, name lookup, qualified
lookup, ADL, and template argument deduction. It also records the observed
C++20 conformance gaps.

## Executive summary

The current architecture is pragmatic and has accumulated substantial support
for common C++20 template cases, but it is not yet a standard-conforming
template system. The main structural issue is that template argument parsing,
classification, lookup, substitution, deduction, and instantiation are still
spread across parser-owned heuristics, global registries, `TypeInfo` side data,
and late AST rewrites.

C++20 requires template argument interpretation to be driven by declaration
context, name lookup, dependency, the current instantiation, two-phase lookup,
deduction rules, constraints, and overload resolution. FlashCpp often decides
the type/value/template-template category before the target template parameter
is authoritative, then repairs or reinterprets the argument later. Several
recent improvements reduce regressions: scalar static-member folding is
explicit with normalized bytes preferred and bounded recursion; struct member
function codegen has per-function IR snapshots that roll back only the failing
member's IR while preserving earlier members; sema constructor annotation is
advisory and codegen falls through to runtime overload resolution when sema
omits it; and enum functional casts are now consistently categorized as
`TypeCategory::Enum` across parser, overload-resolution, and sema type-inference
layers. These improvements reduce regressions but do not yet provide a fully
semantic C++20 template pipeline.

## Refactor completion status

The 2026-05-13 template infrastructure refactor completed the planned first
architecture pass while preserving the behavior described in this audit. The
2026-05-14 follow-up pass advanced the highest-impact remaining template
infrastructure tracks. The branch now includes:

- centralized static `constexpr` member reads through one semantic service;
- ordered and typed NTTP identity preservation, including exact integral/enum
  identity and typed `nullptr` identity;
- shared alias/type-size query infrastructure;
- parameter-context-driven explicit template argument classification;
- `TemplateInstantiationContext` plumbing for template lookup and substitution;
- semantic template lookup requests/records for function templates, member
  templates, qualified owners/calls, variable templates, alias templates, member
  probes, and operator/ADL template lookup;
- conservative template definition lookup records for non-dependent calls and
  broader two-phase lookup timing;
- shape-only template deduction viability checks used before body
  materialization for selected free, explicit free, and member
  function-template overload paths;
- dependent qualified-name records for member-type placeholders, current
  instantiation owners, unknown-specialization owners, and expression
  qualified-ids;
- dependent alias-size, nested pack/materialization, and dependent constexpr
  regression fixes;
- explicit-template SFINAE repair so explicitly supplied template arguments are
  substituted before remaining call-argument deduction;
- typed NTTP identity for integral/enum/`nullptr`, object pointers, references,
  function pointers, and null member pointers, with unsupported structural-class
  cases still explicit.
- qualified non-dependent template-body calls now preserve definition-context
  lookup records on the call expression, including qualified-name metadata;
- floating-point NTTP identity now flows through constexpr evaluation and Itanium
  mangling for standard-shaped floating arguments.
- remaining low-frequency parser template probes and the constexpr
  static-initializer template probe now use parser-built semantic lookup
  requests instead of hardcoded point-of-definition requests.
- `lookup_inherited_template` and `lookup_inherited_type_alias` now follow the
  `type_index` on a deferred base (including one alias-chain level) to find a
  concrete struct and recurse into it rather than always skipping deferred bases.
- `getTemplateParametersForTypeInfo` in `ConstExprEvaluator_Members.cpp` now
  uses `makeTemplateNameLookupRequest` when a parser context is available,
  propagating POI timing and definition-namespace correctly.
- `ExpressionSubstitutor.cpp` existence-check registry calls are documented as
  intentionally direct (simple name-presence check, no instantiation context).
- semantic analysis structured-binding tuple-like lookup now resolves
  `tuple_size`, `tuple_element`, and `get` through parser-owned semantic
  template-name lookup (`Parser::lookupTemplateName`) before specialization
  matching.
- structured-binding tuple-like specialization matching now uses semantic-lookup
  candidate ordering first (`tuple_size`, `tuple_element`, `get`) with no
  synthesized-name fallback.
- tuple-like mixed member/free `get` protocol precedence now prefers
  member `e.get<I>()` over free `get<I>(e)` when both are available.
- fixed root cause of `fallback_names` workaround: `Parser_Templates_Class.cpp`
  now registers struct specializations via the `QualifiedIdentifier` overload of
  `registerSpecialization` so they are stored under both simple and fully
  namespace-qualified names. `std::tuple_size<E>` etc. are now found through
  normal qualified semantic lookup. The non-standard `fallback_names` mechanism
  has been removed entirely from `SemanticAnalysis.cpp`.
- **semantic analyzer unification completed in this slice (2026-05-15):** the
  remaining sema-layer direct template-name lookup probe in
  `SemanticAnalysis.cpp` (structured-binding tuple protocol lookup) now routes
  through the parser-owned semantic lookup interface. A follow-up audit of
  `SemanticAnalysis.cpp`, `OverloadResolution.h`, and `SemanticTypes.h`
  confirms no remaining sema-layer direct template-name lookup probes.
- **`QualifiedTypeMemberAccess::template_arguments` allocation optimized (2026-05-15):**
  the `unique_ptr<vector<TemplateTypeArg>>` field was replaced with a raw pointer
  into `gChunkedAnyStorage`, reducing two heap allocations per dependent member
  chain segment to one and improving cache locality on the hot template
  substitution path.
- **dependent unqualified-call POI completion added (2026-05-15):**
  unresolved dependent unqualified calls now carry first-class
  `DependentUnqualifiedCallLookupRecord` metadata on `CallExprNode`. Template
  substitution, semantic call annotation, constexpr evaluation, and lazy static
  member replay can re-run definition-filtered ordinary lookup plus ADL at the
  point of instantiation, and lazy static replay now restores definition lookup
  context while rebuilding initializer AST.
- **dependent ADL function-template POI completion broadened (2026-05-15):**
  unresolved dependent unqualified call completion now also attempts
  ADL-associated namespace-qualified function-template instantiation at POI
  when ordinary overload sets are empty. Regression
  `test_template_two_phase_dependent_adl_function_template_poi_ret0.cpp`
  covers the case.
- **two-phase lookup extended to member function template bodies (2026-05-15):**
  `instantiate_member_function_template_core` in
  `Parser_Templates_Inst_MemberFunc.cpp` now sets `phase1_cutoff_line_`,
  `phase1_cutoff_file_idx_`, and `current_template_definition_lookup_context_`
  before `parse_function_body()`, mirroring the identical setup in the free
  function template path. Non-dependent calls inside
  `template <typename U> T Wrapper<T>::foo(U)` bodies now resolve against
  definition-time overload sets. `filterPhase1OrdinaryFunctionOverloads` was
  removed from `lookupMemberFunctionTemplateCandidatesForInstantiation` because
  member-lookup visibility is class-scope (mutual, not cutoff by textual order).
  Regression `test_template_two_phase_member_func_template_ret42.cpp` covers
  the non-dependent call case. Full-suite validation: 2361 pass, 184
  expected-fail, 0 regressions.

Validation after all changes passed the Linux sharded build and the full test
suite (2361 regular tests + 184 expected-fail tests, 0 regressions).

The remaining non-conforming areas below are therefore forward-looking
architecture gaps, not known regressions from the refactor.

## What is left / next

The remaining work is not another broad parser cleanup. The next useful passes
should target the places where FlashCpp still lacks standard-owned semantic
state. The highest-impact semantic analyzer unification track is now addressed
in this slice: the last sema-layer direct template-name lookup probe was
rerouted through the parser-owned interface, and audit now shows no remaining
sema direct lookup probes. The active architecture tracks are therefore:

### Open next steps

1. **Broaden POI completion beyond the new unresolved-call record.**
   Unresolved dependent unqualified calls now carry explicit POI-completion
   metadata and replay correctly through substitution, sema, constexpr, and
   lazy-static paths. Remaining gaps are parser-time reparses that become
   concrete before the record is formed and broader hidden-friend/
   dependent-namespace coverage.

2. **Two-phase lookup expansion (member function template bodies ✅ done).**
   Definition-context records now cover selected non-dependent calls,
   unresolved dependent unqualified calls, the main qualified/member/operator
   template paths, lazy static-member replay, and (new) member function
   template bodies. Remaining gaps:
   - **Inherited member templates**: `this->template method<T>(args)` fails
     to find templates declared in a base class.  Root cause: in
     `tryInstantiateMemberFunctionTemplateCall` / `Parser_Expr_PostfixCalls.cpp`
     and `lookupMemberFunctionTemplateCandidatesForInstantiation` /
     `Parser_Templates_Inst_MemberFunc.cpp`, lookup never walks base classes
     for member templates.  Fix: after direct lookup fails, fall through to
     `lookup_inherited_template` and retry instantiation on the owner base.
   - Eager static initializers (constexpr static members whose initializers
     call constexpr functions at definition time).
   - Richer dependent-base lookup and deeper member-template segment chains.

3. **Structural NTTP implementation.**
   Typed integral/enum/`nullptr`, pointer, reference, function-pointer, and null
   member-pointer identity exists. Add real semantic values for non-null
   member-pointers, floating-point, and structural class-type NTTPs, and replace
   the remaining vendor-hash mangling fallback (see `TODO(item-8)` in
   `NameMangling.h`) where possible.

4. **Deduction and constraint pipeline split.**
   Signature-only candidate ranking now covers selected free, explicit free, and
   member function-template paths. Continue replacing remaining
   full-instantiation fallbacks with sema-owned candidate construction,
   deduction, constraints, partial ordering, and overload ranking.

5. **Dependent-name and current-instantiation model.**
   Build on `TypeInfo::DependentQualifiedNameRecord` with richer records for
   deeper expression/member-template chains, dependent bases,
   injected-class-name, and alias ordering.

6. **Constructor fallback hardening.**
   Current valid test coverage no longer needs codegen-time constructor
   fallback, but uncovered/invalid paths still use a soft fallback. Keep widening
   sema coverage until this can become a diagnostic/invariant.

### Highest-impact architecture targets

1. **Semantic analyzer unification on semantic lookup records. ✅ ADDRESSED IN THIS SLICE**
   The remaining sema-layer direct template-name lookup probe
   (`SemanticAnalysis.cpp` structured-binding tuple protocol helper) now routes
   through `Parser::lookupTemplateName()`. Audit across
   `SemanticAnalysis.cpp`, `OverloadResolution.h`, and `SemanticTypes.h`
   confirms no remaining sema direct template-name lookup probes. Intentional
   direct calls that are not lookup probes (e.g., pattern-identity checks and
   no-parser-context fallback paths) remain unchanged.

2. **Two-phase lookup and semantic lookup records.**
    This remains the largest conformance lever. Non-dependent names in templates
    need definition-context lookup records, while dependent names need explicit
    point-of-instantiation completion records. This also gives qualified lookup,
    ADL, static initializers, and member-template calls a shared source of truth.
     **Progress:** conservative definition-context records now guard
     non-dependent unqualified function calls, and semantic lookup records now
     cover the main function/member/qualified/operator template discovery and
     probe paths with dependent POI/ADL behavior regression-tested. Dependent
     unqualified POI completion now also instantiates ADL-associated
     function-template candidates by namespace-qualified names when needed.

3. **Structural NTTP value model.**
   The concrete value identity still has C++20 gaps beyond the supported
   scalar/entity categories. Non-null member-pointer, floating-point, and
   structural class-type arguments still need typed equivalence and hashing.
    **Progress:** the value model now distinguishes typed integral/enum,
    `nullptr`, object pointer, reference, function-pointer, and null
    member-pointer identities, and explicitly rejects unsupported structural
    class-type NTTPs instead of manufacturing scalar identities.
    Remaining: full Itanium encoding for function-pointer, member-pointer NTTPs
    (see `TODO(item-8)` in `NameMangling.h` ~line 1103).

4. **Deduction and constraint pipeline separation.**
   Candidate construction, deduction, constraint checking, partial ordering,
   overload ranking, substitution, and instantiation should keep moving into
    separate sema-owned phases. **Progress:** overloaded free function
    templates, explicit free function-template calls, and implicit member
    function templates can rank signature-only candidates before instantiating
    the selected body.

5. **Dependent-name and current-instantiation model.**
   Replace dependent strings/placeholders with first-class entities for current
   instantiation, unknown specialization, dependent bases, dependent
   qualified-names, and dependent template-ids. **Progress:** dependent
   qualified member-type placeholders, current-instantiation owners,
   unknown-specialization owners, and expression qualified-ids now carry
   `TypeInfo::DependentQualifiedNameRecord` metadata for the covered paths.

## Current architecture snapshot

- `TemplateTypeArg` carries type, non-type, template-template, and pack
  arguments across parser, sema, registry, and codegen layers.
- `NonTypeValueIdentity` preserves typed identity for supported integral, enum,
  `nullptr`, object-pointer, reference, function-pointer, and null
  member-pointer NTTPs. Non-null member-pointer, floating-point, and structural
  class-type NTTPs remain open.
- `TemplateArgIdentity`, `OrderedTemplateInstantiationIdentity`, and
  `TemplateInstantiationKey` preserve parameter-order-sensitive specialization
  identity.
- `TemplateEnvironment`, `TemplateEnvironmentSnapshot`, and
  `TemplateInstantiationContext` are the intended substitution and
  current-instantiation owners, but some legacy paths still reconstruct raw
  parameter-name and argument vectors.

Parameter-context classification is now in place for the main explicit
template-id use sites. Remaining classification risk is now concentrated in
dependent and deep-chain paths rather than low-frequency ordinary probes.

All main and low-frequency template lookup paths (function, member, qualified,
variable, alias, probe, operator, parser/static-initializer, constexpr member
template parameter resolution) now use parser-built semantic lookup
requests/records. Deferred-base-class lookup in `lookup_inherited_template` and
`lookup_inherited_type_alias` now follows the stored `type_index` to a concrete
struct when available. Remaining dependent-base lookup (plain template-parameter
bases), hidden-friend ADL at POI, and deeper member-template chains should still
move behind the same declaration-result and definition-vs-POI timing model.

Static `constexpr` reads are centralized, invalid non-dependent initializers now
hard-fail instead of using broad zero-like recovery, and constructor annotation
coverage is sufficient for current valid tests. The remaining cleanup is to
turn the constructor/codegen fallback and other compatibility repairs into
diagnostics or invariants once sema coverage is broad enough.

Selected free, explicit-free, and member function-template overload paths now
rank signature-only candidates before materializing bodies. Remaining deduction
work should focus on moving non-deduced contexts, forwarding references, packs,
template-template parameters, CTAD/deduction guides, partial ordering, and
constraint subsumption into sema-owned phases before instantiation.

## Standard-conformance assessment

### Mostly aligned areas

- Template parameters have an explicit type / non-type / template-template kind.
- Template argument identity now preserves source order.
- Type arguments carry cv/ref/pointer/array/function-signature metadata.
- Dependent non-type expressions can preserve AST for later reevaluation.
- Template environments and snapshots preserve some outer-template state.
- ADL and hidden-friend infrastructure exists for ordinary calls.
- Lazy materialization has moved toward sema-owned ODR-use handling in recent
  work.
- Static constexpr scalar-member folding is now narrower and more deterministic,
  preferring normalized bytes and bounded recursive base evaluation.
- Struct member function codegen uses per-function IR snapshots; a codegen
  failure in one member rolls back only that member's partial IR, preserving
  successfully emitted earlier members.
- Sema constructor annotation is soft: absent annotation triggers a logged
  warning and a codegen-time overload resolution fallback rather than an
  internal error, failing hard only if codegen resolution also fails.
- Enum functional casts are normalized to `TypeCategory::Enum` at parser
  construction, overload-resolution conversion matching, and sema type-inference
  layers; `ResolvedQualifiedIdentifierInfo` carries a stored `enum_owner_type_index`
  for a direct fast-path type inference on enum constants.

### Non-conforming or structurally risky areas

#### 1. Template argument kind is often heuristic

C++20 template argument interpretation is parameter-context sensitive. FlashCpp
often classifies arguments before the target parameter is available or before
lookup is authoritative. This affects ambiguous simple identifiers, qualified
ids, dependent expressions, aliases, variable templates, and static members.

#### 2. Non-type template arguments are too narrow

C++20 NTTPs include integral values, enumeration values, pointers, references,
member pointers, `nullptr`, floating-point values, and structural class-type
values subject to the C++20 structural-type rules. FlashCpp now has typed
identity for supported integral, enum, `nullptr`, object-pointer, reference,
function-pointer, and null member-pointer arguments, but the remaining NTTP
categories are not yet represented as standard semantic values. Until non-null
member-pointer, floating-point, and structural class values are produced by the
parser and constexpr evaluator, full C++20 NTTP equivalence and mangling remain
incomplete.

#### 3. Dependent names are represented as strings/placeholders

Many dependent facts are still stored as `dependent_name`, placeholder
`TypeInfo`, or optional AST. The follow-up work added
`TypeInfo::DependentQualifiedNameRecord` for important member-type placeholders,
current-instantiation owners, unknown-specialization owners, and expression
qualified-ids, but C++20 requires the same semantic distinction across more
dependent-base, injected-class-name, dependent template-id, and deeper
expression/member-template segment paths. String identity is not enough to
implement these rules reliably.

#### 4. Two-phase lookup is incomplete

The current system still has paths that reparse and substitute using current
parser state, global registries, and symbol tables. Conservative
definition-context records now protect selected non-dependent unqualified
function calls, but the compiler does not yet have a complete
point-of-definition ordinary lookup snapshot plus point-of-instantiation
dependent lookup model. This risks both accepting ill-formed code and rejecting
valid dependent code outside the covered call paths.

#### 5. Qualified lookup is not one authoritative semantic service

Qualified lookup is split across parser type parsing, expression parsing,
registry lookup, string-based qualified names, namespace registry helpers, and
member-template reconstruction. C++20 qualified lookup has precise behavior for
class scopes, base classes, dependent bases, namespaces, injected-class-names,
using-declarations, and access. These need one semantic lookup engine.

#### 6. ADL is not uniformly integrated with templates

Ordinary call parsing has ADL support, but template fallback, template function
candidate lookup, dependent calls, hidden friends, and static initializer
rebinding do not all flow through one call-resolution service. C++20 requires
function template candidates found by ADL to participate in overload resolution
and two-phase lookup.

#### 7. Deduction and overload resolution are interleaved with instantiation

C++20 separates candidate construction, template argument deduction, constraint
checking, viability, partial ordering, overload resolution, substitution, and
instantiation. FlashCpp now has signature-only ranking for selected overloaded
free and member function-template paths, but other paths still mix these steps
while searching the registry and materializing AST.

#### 8. Static member handling is too eager and repair-oriented

Static member initializers are rebound, substituted, instantiated, and
constant-evaluated inside class-template instantiation. This can work for traits,
but C++20 requires precise instantiation timing, constant-evaluation context,
definition availability, and odr-use behavior.

The current recursive and normalized constant paths are still tactical repairs
in codegen/parser-owned flows rather than a fully sema-owned materialization
model.

#### 9. Current-instantiation identity is incomplete

Outer bindings and owner reconstruction support many member-template cases, but
the compiler does not yet maintain an authoritative current-instantiation model
for all qualified names, nested classes, dependent bases, injected-class-names,
and member access.

## Conclusion

The current architecture is useful and increasingly capable, but it is not
standard compliant as a whole. The highest-priority architectural issue is not a
single parser bug; it is the absence of one semantic template system that owns:

1. declaration lookup and dependency classification;
2. template parameter and argument matching;
3. two-phase lookup;
4. deduction and constraints;
5. substitution and point-of-instantiation materialization;
6. constant evaluation under an instantiated semantic context.

## Remaining implementation plan

1. **Unify semantic lookup results**
   Create one lookup result type used by template candidate discovery, qualified
   lookup, member lookup, ADL-sensitive call resolution, and constexpr/static
   initializer paths. Template registries should become indexes behind semantic
   lookup rather than independent lookup authorities.

2. **Expand definition-vs-POI records**
   Extend the existing non-dependent function-call records to qualified names,
   member-template calls, static initializers, dependent bases, unknown
   specializations, and expression qualified-ids.

3. **Complete remaining structural NTTP values**
   Add parser/constexpr support for non-null member-pointer, floating-point, and
   structural class-type NTTP values, then wire those categories through
   identity, hashing, mangling, specialization lookup, and diagnostics. Replace
   conservative function/member-pointer mangling fallbacks where standard entity
   encodings are available.

4. **Broaden signature-only deduction/ranking**
   Move more overload and deduction paths onto shape-only candidate viability so
   ranking does not instantiate losing bodies. Include constraints,
   non-deduced contexts, forwarding references, packs, template-template
   parameters, CTAD, deduction guides, and partial ordering.

5. **Finish dependent-name/current-instantiation modeling**
   Replace remaining string-like dependent placeholders with records for current
   instantiation, dependent base members, unknown specializations, injected class
   names, dependent template-ids, and expression qualified-ids.

6. **Convert repair paths into invariants**
   Keep the current compatibility fallbacks only where they are still needed for
   uncovered cases. As sema coverage expands, convert constructor fallback,
   lookup fallback, and materialization fallback paths into explicit diagnostics
   or internal invariants.
