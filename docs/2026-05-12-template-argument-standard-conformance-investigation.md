# Template Argument Standard-Conformance Investigation

**Date:** 2026-05-12  
**Last updated:** 2026-05-25 (primary-template nested OOL constructor-template attachment now resolves replay-first via source-member→stub identity with overload-safe StructTypeInfo sync)

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
  outer parameter types while copying definition-side parameter identifiers;
- **partial-spec member-function-template instantiation now produces
  owner-correct nodes with registered qualified names and outer-template
  bindings**, so the existing deferred-body replay path can reparse those
  bodies with `T→concrete` in scope without needing a special partial-spec
  fallback.

- **partial-spec out-of-line plain (non-template) member functions now have
  their bodies parsed and substituted immediately during partial-spec
  instantiation**, enabling dependent-base lookup via `this->...` through
  partial specs without relying on partially substituted AST state.
- **partial-spec non-ctor out-of-line member attachment now checks both the
  current template name and base template name (without duplicate replay)**,
  covering both plain members and deferred member-function-template replay when
  registration lands under the base template key.
- **primary-template and partial-spec plain (non-template) out-of-line member
  attachment now use replay-first source-member→instantiated-stub identity
  lookup (including same-name overloads)**, removing the old
  instantiated-member name/arity attachment scan for that slice.
- **primary-template plain out-of-line constructor attachment now also uses
  replay-first source-member→instantiated-stub identity lookup (including
  same-name overloads)**: constructor replay disambiguates via substituted
  signatures and updates `StructTypeInfo` constructor bodies with
  signature-equivalent matching instead of name-only syncing.
- **partial-spec out-of-line constructor-template attachment now also uses
  replay-first source-member→instantiated-stub identity lookup (including
  same-name overloads)**: constructor-template attachment disambiguates via
  substituted signatures and updates `StructTypeInfo` constructor-template
  metadata with signature-equivalent matching instead of scan/name-only sync.
- **nested-class out-of-line constructor-template attachment now also uses
  replay-first source-member→instantiated-stub identity lookup (including
  same-name overloads)**: nested replay disambiguates constructor-template
  targets through source-member identity and updates nested `StructTypeInfo`
  constructor-template metadata via signature-equivalent matching.
- **primary-template nested out-of-line constructor-template attachment now also
  uses replay-first source-member→instantiated-stub identity lookup (including
  same-name overloads)**: nested constructor-template targets are now attached
  through source-member identity with signature-equivalent `StructTypeInfo`
  synchronization.
- **declaration-only member stub substitution now strips only top-level
  by-value cv qualifiers**: pointer/reference pointee cv metadata is preserved,
  so replay-first source-member signature matching and mangled identity remain
  stable for out-of-line plain members (including classes that also declare
  constructors).
- **deferred class-template constructor replay now uses normalized template
  environments for substitution registration**, preserving full typed NTTP
  identity payloads (pointer/reference/function-pointer/member-pointer) and
  reusing the shared value-parameter substitution path instead of a
  constructor-only numeric fallback.
- **`P::method()` qualified calls where P is a template type parameter are now
  resolved at sema level**: `resolveQualifiedOwnerType` in `tryRecoverCallDeclFromStructMembers`
  now walks `InstantiationContext::param_names`/`param_args` when the qualifier
  is a simple name that matches a template type parameter. This covers the
  policy-dispatch pattern (`template<typename Policy> struct S { void f() { Policy::method(); } }`)
  without requiring a codegen-only recovery path. A parallel codegen safety net
  is retained.
- **C++20 extended aggregate initialization through base-class-only intermediates
  is handled at codegen level**: when `resolveCodegenConstructorFromArgs` returns
  null for an aggregate struct with no direct members but with base classes,
  codegen emits a default-construct for the aggregate and a forwarded
  ConstructorCallOp for the first matching inner base constructor. Sema cannot
  represent this at the `ConstructorCallNode` level today (single
  `resolved_constructor` pointer). This is tracked as a lower-priority open item:
  extend `ConstructorCallNode` to carry aggregate-forwarding metadata so sema can
  own the two-call sequence.
- **deferred dependent-base metadata is now persisted in `StructTypeInfo` as full
  `DeferredTemplateBaseClassSpecifier` entries (not only template names)**, and
  inherited member-template owner lookup now consumes those specifiers (including
  member-type chains) before falling back to legacy name-only traversal.
- **in-class and out-of-line template static-member initializers now enforce
  replay metadata invariants for dependent/complex initializers**: these paths
  now fail fast on missing replay metadata instead of silently using broad
  AST-only substitution fallback.
- **primary-template nested out-of-line member-template attachment now uses
  source-member→instantiated-stub identity mapping (AST-node + declaration
  location identities)**: same-name overload disambiguation now validates
  substituted signatures on identity-resolved stubs, and this path no longer
  relies on instantiated-candidate scan fallback or original type-info recovery.
- **partial-spec nested out-of-line member-template attachment now uses the
  same replay-first source-member→instantiated-stub identity mapping (AST-node
  + declaration-location identities)**: same-name overload disambiguation now
  validates substituted signatures on identity-resolved stubs, and this slice
  no longer relies on instantiated-member scan-first matching.

Latest recorded full-suite validation:
`2501` regular tests compiled/linked/runtime-pass, `0` fail, `181` expected-fail tests.

Latest focused regressions added on the current branch:
- `test_template_nested_ool_member_template_outer_param_binding_ret0.cpp`
- `test_template_nested_ool_member_template_arity_disambiguation_ret0.cpp`
- `test_template_ool_ctor_template_param_rename_replay_ret0.cpp`
- `test_template_partial_spec_ool_member_template_two_phase_lookup_ret0.cpp`
- `test_template_partial_spec_ool_plain_member_ret0.cpp`
- `test_template_partial_spec_ool_member_template_base_name_lookup_ret0.cpp`
- `test_template_partial_spec_ool_plain_member_base_name_lookup_ret0.cpp`
- `test_template_nttp_deferred_ctor_body_ret0.cpp`
- `test_template_aggregate_base_class_ctor_ret0.cpp`
- `test_template_type_param_qualified_static_call_ret0.cpp`
- `test_template_deferred_base_member_chain_template_lookup_ret0.cpp`
- `test_template_static_member_initializer_replay_metadata_invariant_ret0.cpp`
- `test_template_nttp_deferred_ctor_body_pointer_function_ret0.cpp`
- `test_template_ool_plain_member_same_name_overload_ret0.cpp`
- `test_template_partial_spec_ool_plain_member_same_name_overload_ret0.cpp`
- `test_template_ool_ctor_same_name_overload_ret0.cpp`
- `test_template_partial_spec_ool_ctor_template_same_name_overload_ret0.cpp`
- `test_template_nested_ool_ctor_template_same_name_overload_ret0.cpp`
- `test_template_primary_nested_ool_ctor_template_same_name_overload_ret0.cpp`
- `out_of_line_template_member_with_ctor_ret0.cpp`

## Remaining work, in priority order

### 1. Remove the next replay-metadata gap

Continue with the remaining declaration replay paths outside static-member
initializers that still never captured enough metadata at parse time.

Rule for this work:

- prefer replay-first semantic attachment;
- do not add new AST-only repair paths unless they are strictly temporary and
  documented.
- next slices:
  - remove the remaining constructor/non-static declaration replay scans still
    not keyed by source-member identity; then tighten constructor-template
    signature matching to reduce shape-based fallback when substituted
    signatures are unavailable.

### 2. Expand dependent-name/current-instantiation modeling only as needed

Still open:

- richer dependent-base records;
- more unknown-specialization coverage;
- deeper member-template/type chains;
- consistent current-instantiation identity across qualified-name paths.

This work should support item 1, not replace it as the main track.

### 3. Leave lower-priority tracks for later unless they block 1-2

Still open, but not the next best slice:

- broader sema-owned deduction and ranking;
- final conversion of repair paths into invariants/diagnostics;
- **C++20 extended aggregate initialization at sema level**: `ConstructorCallNode`
  holds a single `resolved_constructor` pointer, which cannot model the
  two-constructor sequence (default-init the aggregate, then forwarded-init the
  inner base). Codegen fallback in `IrGenerator_Visitors_Decl.cpp` handles the
  pattern correctly today. Uplift to sema requires extending `ConstructorCallNode`
  to carry aggregate-forwarding metadata (inner base ctor reference + combined
  offset).

## Recommended implementation order

1. remove remaining declaration replay scans in non-static template-member
   attachment paths, starting with constructor attachment/replay paths that
   still recover targets from instantiated members instead of source members
   (next: tighten constructor-template matching to canonical substituted-signature
   matching where shape fallback is still used);
   keep function-parameter adjustment rules centralized so replay/attachment
   compares canonical signatures across eager/lazy/declaration-only paths;
2. update these docs with the next remaining replay-metadata gap.

## Regression focus

Keep adding narrow regressions in these areas:

- replayed out-of-line member-function-template bodies using `this->template`
  or equivalent dependent-base lookup — particularly through partial specs;
- declaration/definition attachment where inner and outer template parameters
  interact;
- remaining non-static declaration replay paths that still fall back to
  partially substituted AST state, especially constructor-template attachment
  paths that still require shape-based fallback disambiguation.

## Exit criteria

This plan is complete when:

- the main replay paths preserve definition-time vs POI lookup timing through
  semantic metadata instead of fallback recovery;
- dependent-name/current-instantiation behavior is semantically modeled rather
  than reconstructed from placeholder spellings;
- remaining repair paths have been converted into diagnostics or invariants;
- ordinary overload selection does not materialize losing candidates by
  default.
