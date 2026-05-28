# Template Argument Standard-Conformance Investigation

**Date:** 2026-05-12  
**Last updated:** 2026-05-28 (nested member-template alias materialization now preserves outer owner/member-template metadata through explicit template-argument parsing and alias rebinding)

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
- **out-of-line constructor replay attachment now requires canonical
  substituted-signature evidence across both plain and constructor-template
  helper paths**: single-candidate unresolved (`nullopt`) acceptance has been
  removed; no-match cases now surface explicit replay-attachment diagnostics
  instead of silent fallback attachment.
- **constructor-template materialization now reuses an owner-local constructor
  lookup instead of repeated scans**: `materializeMatchingConstructorTemplate`
  now indexes constructors once per owner and reuses direct source identity,
  lazy registry keys, signature-equivalent body-source recovery, and cached
  mangled-name matches for preferred-ctor resolution, access propagation, and
  duplicate-materialization checks.
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
- **nested-struct template member OOL scan now uses signature-based
  disambiguation for overloads**: the prior name+arity-only scan mis-attached
  OOL bodies when two template member functions in a nested struct shared a
  name and inner-template-parameter count but differed in non-template parameter
  types; replaced with a `nestedOutOfLineMemberTemplateMatchesCandidate`-gated
  match (using the established `same_name_count > 1` strictness gate), with a
  `break` after the first successful attachment and error logging on no-match.
- **nested constructor-template OOL attachment path that previously used
  local signature-scan-first matching now routes through the replay-first
  `findOutOfLineConstructorTemplateStubByIdentity` helper** (constructor-node
  and function-node definitions), so selection remains source-member-identity
  centered and canonical-substitution based.
- **nested constructor source-member preselection in
  `nested_source_member_identity_maps` no longer uses signature-equivalence
  scan against `nested_struct_info->member_functions`**: registration now uses
  direct source-member identity mapping for constructors and non-constructors.
- **constructor synchronization into `StructTypeInfo` now first checks direct
  constructor-node identity before signature-equivalence scan**, reducing
  dependence on scan-only matching when replay attachment already resolved a
  concrete constructor node.
- **replay signature placeholder detection now uses
  `typeSpecStillUsesDependentPlaceholder(...)`** (instead of only direct
  `TypeInfo::isDependentPlaceholder()`), improving dependent signature
  classification coverage in canonical replay matching.
- **non-constructor nested/member-template out-of-line replay disambiguation no
  longer uses shape fallback when substituted-signature comparison returns
  `nullopt`**: these paths now require canonical substituted-signature evidence
  for positive attachment, and strict disambiguation pre-counts are narrowed to
  relevant candidates (same name + inner template-parameter count + function
  parameter count).
- **constructor and constructor-template out-of-line replay signature matching
  now canonicalizes dependent placeholder/member-type identity** (including
  dependent qualified-name records and effective alias/indirection metadata),
  so valid replay-first attachments that previously collapsed to unresolved
  substituted-signature outcomes now classify as canonical matches without
  reintroducing single-candidate fallback acceptance.
- **after substituted-signature mismatch, OOL matching now performs a strict
  owner-artifact dependent-member recovery step**: when one side collapses to an
  owner type, matching attempts qualified member-type resolution (`Owner::member`
  / dependent member-chain) and only accepts if canonical type identity matches
  the other side.
- **dependent-member swapped out-of-line constructor replay now preserves the
  definition-side parameter identifiers on identity-resolved constructor stubs
  and their `StructTypeInfo` copies**, then re-enqueues the owning instantiated
  struct for semantic normalization after late body materialization. This keeps
  constructor-call and initializer annotations aligned with the replay-selected
  body before IR generation, closing the first downstream swap regression
  exposed after owner-artifact recovery.
- **template-parameter-qualified static-member expression typing now resolves
  simple type-parameter qualifiers through the current instantiated member
  context**, so replayed bodies such as `auto x = T::value;` deduce `auto` from
  the concrete static member instead of leaving a placeholder for codegen.
  Template-body substitution also re-deduces local placeholder variables after
  initializer substitution.
- **OOL member-function-template replay signature matching now resolves concrete
  type-parameter-qualified member-template type chains**, including
  `typename T::template AddPtr<int>::type` parameter types. The
  recovery remains evidence-driven: dependent member-template chain
  segments are materialized through concrete owner bindings and only
  accepted when canonical type identity matches, without restoring
  broad shape fallback attachment.
- **OOL member-function-template body replay now preserves concrete alias
  metadata for type-parameter-qualified member-template type parameters**:
  direct dereference, forwarding, and reference-to-alias-parameter forwarding of
  `typename T::template AddPtr<int>::type` now materialize body-local parameter
  metadata as the resolved pointer type instead of a zero-sized alias placeholder.
  Alias-owner recovery is narrowed to evidence-backed dependent-owner records or
  a unique concrete struct owner, so existing alias-template default NTTP and
  non-template-intermediate member-alias paths remain stable.
- **OOL member-function-template overloads whose parameters differ only by
  concrete type-parameter-qualified member-template alias targets now preserve
  dependent owner/member-template identity across copied instantiated stubs,
  replay attachment, lazy member-template shape instantiation, and concrete
  alias materialization**: `typename T::template AddPtr<int>::type` vs
  `...AddPtr<long>::type` no longer collapses to the same owner artifact or the
  same `$olN` body. The adjacent plain-member dependent-qualified replay path
  now concretizes those definition-side alias targets after attachment, and the
  nested dependent-member swap replay path now concretizes non-template
  dependent member aliases before overload-shape reuse.
- **nested member-template alias arguments now preserve outer owner/member-template
  metadata through explicit-template-argument parsing and alias rebinding**:
  qualified alias-template arguments such as `Use<Fn>::Alias<int>` stay on the
  type path instead of being misclassified as dependent value expressions, and
  deferred aliases like `Provider<T>::Node::template Apply<U>` now keep the full
  owner/member chain plus concrete alias arguments during placeholder rebinding
  and materialization. Covered nested alias uses therefore resolve through
  semantic dependent-name records instead of collapsing to terminal-name lookup.
- **dependent alias resolution now re-enters semantic owner/member-chain
  resolution after direct alias substitution before touching the legacy
  `base::member` textual path**: covered alias-template chains no longer depend
  on terminal-name reconstruction once the substituted target already carries a
  semantic dependent-qualified-name record.

Latest recorded full-suite validation:
`2598` regular tests compiled/linked/runtime-pass, `0` fail, `185` expected-fail tests.

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
- `test_template_ool_ctor_same_name_overload_template_default_arg_ret0.cpp`
- `test_template_ool_plain_ctor_nullopt_single_candidate_no_attach_ret0.cpp`
- `test_template_ool_ctor_template_nullopt_single_candidate_no_attach_ret0.cpp`
- `test_template_nested_ool_ctor_template_outer_inner_param_rename_ret42.cpp`
- `out_of_line_template_member_with_ctor_ret0.cpp`
- `test_template_nested_ool_member_template_overload_ret0.cpp`
- `test_template_ool_ctor_dependent_member_swap_body_ret0.cpp`
- `test_template_ool_member_template_dependent_member_swap_body_ret0.cpp`
- `test_template_param_qualified_static_member_auto_ret0.cpp`
- `test_template_ool_member_template_dependent_member_template_alias_overload_ret0.cpp`
- `test_template_ool_member_template_dependent_member_template_type_param_deref_ret0.cpp`
- `test_template_ool_member_template_dependent_member_template_type_param_forward_deref_ret0.cpp`
- `test_template_ool_member_template_dependent_member_template_type_param_ref_forward_ret0.cpp`
- `test_member_template_alias_preserves_outer_metadata_ret0.cpp`

## Remaining work, in priority order

### 1. Remove the next replay-metadata gap

Continue with the remaining declaration replay paths outside static-member
initializers that still never captured enough metadata at parse time.

Rule for this work:

- prefer replay-first semantic attachment;
- do not add new AST-only repair paths unless they are strictly temporary and
  documented.
- next slices:
- delete the now-narrowed terminal-name / `base::member` string-split alias lookup
  in `resolveDependentMemberAlias(...)` once the remaining recordless legacy
  callers preserve full semantic owner/member-chain metadata end-to-end;
- continue improving replay metadata capture for unresolved substitution
  (`nullopt`) outcomes in the remaining non-constructor declaration replay
  paths so canonical substituted-signature classification can succeed in more
  valid cases.
- continue looking for remaining OOL dependent-member swap regressions in
  less-covered member-template and nested replay surfaces now that the first
  constructor replay/body-selection gap has been closed.
- extend dependent/current-instantiation lookup only where needed to unblock
  replay attachment for still-failing type-parameter-qualified member-template
  and member-type references in nested/overloaded member-template surfaces.
- continue looking for remaining declaration-copy or lazy-shape surfaces where
  dependent owner/member-template identity is still erased too early or
  preserved too long, especially in deeper owner/member-template chains beyond
  the covered `AddPtr<int>`/`AddPtr<long>` overload case.

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

1. improve replay metadata capture in the remaining non-static
   template-member attachment paths so unresolved (`nullopt`)
   substituted-signature outcomes classify more often and attachment remains
   evidence-driven without broad compatibility fallback;
   keep function-parameter adjustment rules centralized so replay/attachment
   compares canonical signatures across eager/lazy/declaration-only paths;
   prioritize remaining OOL dependent-member swap follow-ups in member-template
   or nested overload sets where replay-selected stubs and deferred body parsing
   can still drift after owner-artifact recovery;
   continue into deeper owner/member-template chains and remaining
   declaration-copy / lazy-shape identity gaps beyond the covered
   `T::AddPtr<int>` vs `T::AddPtr<long>` overload case;
2. update these docs with the next remaining replay-metadata gap.

## Regression focus

Keep adding narrow regressions in these areas:

- replayed out-of-line member-function-template bodies using `this->template`
  or equivalent dependent-base lookup — particularly through partial specs;
- declaration/definition attachment where inner and outer template parameters
  interact;
- remaining non-static declaration replay paths that still fall back to
  partially substituted AST state, especially paths that still produce unresolved
  (`nullopt`) substituted-signature outcomes because replay metadata was not
  captured early enough.

## Exit criteria

This plan is complete when:

- the main replay paths preserve definition-time vs POI lookup timing through
  semantic metadata instead of fallback recovery;
- dependent-name/current-instantiation behavior is semantically modeled rather
  than reconstructed from placeholder spellings;
- remaining repair paths have been converted into diagnostics or invariants;
- ordinary overload selection does not materialize losing candidates by
  default.
