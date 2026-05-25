# Template Argument Architecture Audit

**Date:** 2026-05-12  
**Last updated:** 2026-05-25 (constructor replay signature matching now canonicalizes dependent placeholder/member-type identity so previously unresolved nullopt attachment cases replay correctly without fallback)

This document should stay forward-facing. It is not a historical ledger or
release log. Keep only the minimum completed-state context needed to explain
the current architecture and the next highest-impact work.

## Executive summary

FlashCpp now handles many practical C++20 template cases, but the main
remaining standards gap is still two-phase lookup across replayed template
infrastructure.

The highest-impact work is no longer broad parser cleanup. It is removing the
next replay-metadata gaps that still force template instantiation to recover
intent from partially substituted AST state.

## Current state

Useful assumptions before changing this area:

- the main template lookup paths already prefer semantic lookup records over
  raw parser heuristics;
- covered non-dependent template-body lookup already preserves
  definition-context binding;
- several replay-heavy paths now preserve enough metadata to reparse from
  source instead of depending on AST-only substitution;
- covered dependent owner/member-template chains already reuse a shared
  owner-aware materialization path;
- out-of-line constructor-template replay now preserves inner template metadata
  and matches renamed inner template parameters in the covered paths;
- nested out-of-line member-function-template replay now preserves
  instantiated outer parameter bindings while still copying definition-side
  parameter names;
- **partial-spec member-function-template instantiation now builds new
  owner-correct nodes and registers qualified-name + outer binding entries**
  so the existing replay path can find and replay those bodies with
  `T→concrete` in scope, matching the primary-template path;
- **partial-spec out-of-line plain (non-template) member functions now have
  their bodies parsed and substituted immediately during partial-spec
  instantiation**, matching the primary-template OOL plain-member path and
  enabling dependent-base lookup via `this->...` through partial specs.
- **partial-spec non-ctor out-of-line member attachment now checks both the
  current template name and the extracted base template name (with dedupe)**,
  so plain-member replay and member-function-template deferred replay still
  attach when registrations land under the base template name.
- **primary-template and partial-spec plain (non-template) out-of-line member
  attachment are now replay-first and identity-map-backed (including same-name
  overloads)**: these paths now resolve the source declaration first, then map
  source-member→instantiated-stub identity before replaying the body, removing
  the old instantiated-member name/arity scan in this slice.
- **primary-template plain out-of-line constructor attachment is now replay-first
  and identity-map-backed (including same-name overloads)**: constructor replay
  now resolves source constructor declarations through source-member→stub
  identity, disambiguates overloads via substituted signature matching, and
  synchronizes `StructTypeInfo` constructor bodies by matched instantiated
  signature instead of name-only updates.
- **partial-spec out-of-line constructor-template attachment is now replay-first
  and identity-map-backed (including same-name overloads)**: constructor-template
  attachment now resolves source constructor declarations through
  source-member→stub identity before setting deferred body metadata, and
  synchronizes `StructTypeInfo` constructor-template metadata by signature rather
  than scan/name-only matching.
- **nested-class out-of-line constructor-template attachment now also resolves
  replay-first through source-member→stub identity (including same-name
  overloads)**: nested replay now maps source constructor declarations to
  instantiated stubs first, applies deferred body/initializer metadata on the
  identity-resolved target, and syncs nested `StructTypeInfo` constructor-template
  metadata via signature-equivalent matching.
- **primary-template nested out-of-line constructor-template attachment is now
  replay-first and identity-map-backed (including same-name overloads)**:
  constructor-template stub attachment now resolves source constructor declarations
  through source-member→stub identity, with overload-safe `StructTypeInfo`
  synchronization by signature-equivalent matching.
- **out-of-line constructor replay attachment now requires canonical
  substituted-signature evidence across both plain and constructor-template
  helper paths**: single-candidate unresolved (`nullopt`) acceptance has been
  removed; no-match cases now surface explicit replay-attachment diagnostics
  instead of silent fallback attachment.
- **declaration-only member stub substitution now strips only top-level
  by-value cv qualifiers**: pointer/reference pointee cv metadata is preserved,
  keeping replay-first source-member signature matching and mangled identity
  stable for out-of-line plain members (including classes that also declare
  constructors).
- **deferred class-template constructor replay now builds substitutions from a
  normalized template environment**, so non-type substitutions preserve full
  typed identity payloads (pointer/reference/function-pointer/member-pointer) in
  addition to integral values; replay now reuses the shared
  `trySubstituteValueTemplateParameterExpression` path instead of a
  constructor-only numeric fallback.
- **C++20 extended aggregate initialization through base-class-only intermediates
  now works at codegen level**: when a base initializer resolves to an aggregate
  struct with no direct members but one or more base classes, codegen emits a
  default-construct for the aggregate followed by a forwarded ConstructorCallOp
  to the first inner base whose constructor accepts the given arguments. This is
  a codegen-level fallback appropriate to the current AST model (a single
  `resolved_constructor` pointer on `ConstructorCallNode` cannot represent the
  two-call pattern at sema level without additional node metadata).
- **`P::method()` qualified calls where P is a template type parameter are now
  resolved at sema level**: `SemanticAnalysis::tryRecoverCallDeclFromStructMembers`
  extended `resolveQualifiedOwnerType` to walk the current member context's
  `InstantiationContext::param_names`/`param_args` when the qualifier is a
  simple name that matches a template type parameter. Codegen retains a parallel
  fallback via `InstantiationContext` for cases sema does not yet reach.
- **deferred dependent-base metadata is now persisted as full specifiers in
  `StructTypeInfo` (not only base template names)**, and inherited member-template
  owner traversal now consumes that richer metadata (including member-type chains)
  before falling back to legacy name-only paths.
- **in-class and out-of-line template static-member initializers now enforce
  replay metadata invariants for dependent/complex initializers**: these paths
  now throw invariant failures when required replay metadata is missing instead
  of silently relying on broad AST-only substitution fallback.
- **primary-template nested out-of-line member-template attachment is now
  replay-first and identity-map-backed (including same-name overloads)**:
  source-member→stub attachment now registers and resolves both AST-node and
  declaration-location identities, and overload disambiguation compares
  substituted signatures against the identity-resolved instantiated stub.
  Instantiated-candidate scan fallback has been removed from this primary path.
- **partial-spec nested out-of-line member-template attachment is now
  replay-first and identity-map-backed (including same-name overloads)**:
  source-member→stub identity is now registered during partial-spec member
  instantiation and used for nested OOL attachment/disambiguation, with the
  old instantiated-member scan-first path removed for this slice.
- **nested-struct template member OOL scan now uses signature-based
  disambiguation for overloads**: the name+arity-only scan that previously
  mis-attached OOL bodies when two template member functions shared a name and
  inner-template-parameter count but differed in non-template parameter types
  has been replaced with a `nestedOutOfLineMemberTemplateMatchesCandidate`-gated
  match (using the `same_name_count > 1` strictness gate). The loop now also
  breaks after the first successful attachment and logs an error on no-match.
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

Latest recorded full-suite validation:
`2557` regular tests compiled/linked/runtime-pass, `0` fail, `183` expected-fail tests.

Latest focused replay regressions added on the current branch:
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

## What is still wrong

### 1. Two-phase lookup and replay ownership are still incomplete

This is still the main conformance problem.

The remaining issue is not that replay exists. The issue is that some replay
paths still do not capture enough semantic metadata at parse time, so later
instantiation has to recover intent from partially substituted AST state.

The next highest-value remaining surface:

- remaining declaration replay paths outside static-member initializers that
  still recover intent from partially substituted AST state.
  - remaining constructor and non-constructor replay paths still produce
    unresolved substitution outcomes (`nullopt`) because required replay-visible
    metadata is not always captured early enough; these need metadata completion
    so canonical matching succeeds more often without broad fallback machinery.
### 2. Dependent-name modeling is still too weak

`DependentQualifiedNameRecord` is useful, but it is still not a complete
`[temp.dep]` model. The remaining gaps matter mainly because they block the
replay/lookup work above.

### 3. Lower-priority work remains open

These are still incomplete, but they are not the next best use of effort unless
they directly block items 1-2:

- NTTP completion for the remaining C++20 categories;
- broader sema-owned deduction/ranking;
- final removal of repair-oriented fallback paths.
- **C++20 extended aggregate initialization at sema level**: `ConstructorCallNode`
  carries a single `resolved_constructor` pointer which cannot represent the
  two-call pattern (default-init outer aggregate + forwarded-init inner base).
  Until the AST is extended, this remains a codegen-only fallback
  (`IrGenerator_Visitors_Decl.cpp`). A proper sema fix would require extending
  `ConstructorCallNode` to carry aggregate-forwarding metadata (inner base
  constructor reference + combined offset).

## Highest-impact next steps

1. **Remove the next remaining declaration replay scans outside static-member initializers**
   - Continue with remaining declaration replay scans that still recover targets
     from partially substituted instantiated members instead of source-member
     identity.
   - Continue with remaining attachment/synchronization surfaces that still
     depend on signature-only matching where source-member identity can be
     preserved end-to-end.
   - Improve replay metadata capture in unresolved substitution (`nullopt`) paths
     so canonical substituted-signature matching classifies more valid code.
   - Keep function-parameter adjustment rules centralized in shared substitution
     helpers so replay/attachment compares canonical signatures instead of
     path-specific normalized variants.

2. **Strengthen dependent-name/current-instantiation modeling only where it unblocks 1**
   - Expand richer dependent-base and unknown-specialization records only when
     required by the replay/lookup path above.
   - Avoid broad redesign work that does not directly reduce fallback behavior.

## Short completed-state summary

The following are complete enough to rely on:

- semantic lookup records back the main template lookup paths;
- covered non-dependent template-body lookup preserves definition-time binding;
- covered owner/member-template chains now stay on a shared owner-aware
  materialization path;
- out-of-line static-member replay preserves replay-visible template
  parameters in the covered paths;
- in-class and out-of-line static-member initializer replay now enforces
  replay-metadata invariants for dependent/complex initializers, and no longer
  silently falls back to broad AST-only substitution in those required paths;
- top-level out-of-line constructor-template replay preserves inner template
  metadata and reattaches deferred body/initializer-list state correctly in the
  covered paths;
- nested out-of-line member-function-template replay preserves instantiated
  outer parameter types while importing definition-side parameter names, and now
  attaches definitions through source-member→stub identity (AST-node +
  declaration-location keyed), including same-name overload disambiguation
  against identity-resolved stubs, with no instantiated-candidate scan fallback
  in the primary-template path;
- plain (non-template) out-of-line member replay for both primary templates and
  partial specializations now also resolves through source-member→stub identity,
  including same-name overload cases, with no instantiated-member name/arity
  attachment scan in that slice;
- primary-template plain out-of-line constructor replay now also resolves
  source-member→stub identity first (including overload disambiguation by
  substituted signature), and `StructTypeInfo` constructor-body synchronization
  in this path no longer uses name-only matching;
- partial-spec out-of-line constructor-template attachment now mirrors that
  replay-first source-member→stub identity flow (including overload
  disambiguation), and `StructTypeInfo` constructor-template metadata sync in
  this path no longer relies on scan/name-only matching;
- constructor replay-first attachment no longer admits unresolved
  single-candidate fallback acceptance in plain or constructor-template helper
  paths; canonical substituted-signature evidence is now required for positive
  attachment, and no-match surfaces explicit diagnostics;
- nested-class out-of-line constructor-template attachment now also resolves
  source-member→stub identity first (including same-name overload handling), and
  nested `StructTypeInfo` constructor-template metadata sync in this path no
  longer relies on scan/name-only matching;
- primary-template nested out-of-line constructor-template attachment now mirrors
  that replay-first source-member→stub identity path, with no
  instantiated-member scan-first target selection in this slice;
- partial-spec nested out-of-line member-template attachment now mirrors that
  replay-first source-member→stub identity path (including same-name overload
  disambiguation), with no scan-first fallback in this slice;
- partial-spec plain (non-template) out-of-line member functions now parse and
  substitute their bodies with the correct class-template parameters and
  definition lookup context in scope;
- partial-spec member-function-template instantiation now builds owner-correct
  nodes, registers qualified names, and attaches outer template bindings so the
  replay path can materialize bodies with the correct class-template parameters
  in scope.
- deferred class-template constructor bodies now store template-parameter names
  at parse time, so replay sets `hasActiveTemplateParameters() = true`; replay
  substitution now preserves full typed NTTP identity metadata and reuses the
  shared value-parameter substitution path (no constructor-only identifier
  fallback when typed identity is available).
- **`P::method()` qualified calls where P is a template type parameter are now
  resolved at sema level**: `SemanticAnalysis::resolveQualifiedOwnerType` walks
  the current member context's `InstantiationContext::param_names`/`param_args`
  when the qualifier is a simple name matching a template type parameter.
  A parallel codegen fallback is retained as a safety net.
- deferred dependent-base metadata now persists in `StructTypeInfo` as full
  `DeferredTemplateBaseClassSpecifier` entries, and inherited member-template
  owner traversal consumes those specifiers (including member-type chains)
  before the legacy name-only fallback.
- **C++20 aggregate initialization through base-class-only intermediate structs
  is handled at codegen level** (`IrGenerator_Visitors_Decl.cpp`): when
  `resolveCodegenConstructorFromArgs` returns null for an aggregate struct with
  no direct members but with base classes, codegen emits a default-construct for
  the aggregate followed by a forwarded ConstructorCallOp to the first inner base
  whose constructor accepts the given arguments. Moving this to sema level would
  require extending `ConstructorCallNode` to carry two-call aggregate-forwarding
  metadata; tracked as a lower-priority open item.

## Exit criteria

This audit can be retired when:

- the main replay paths no longer require AST-only fallback for valid template
  code;
- definition-time vs POI lookup timing is preserved across the main template
  body, member, static-member, and replay paths;
- dependent-name/current-instantiation behavior is semantically modeled rather
  than reconstructed from placeholder spellings;
- ordinary overload ranking no longer materializes losing candidates by
  default.
