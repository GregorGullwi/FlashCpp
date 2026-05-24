# Template Argument Architecture Audit

**Date:** 2026-05-12  
**Last updated:** 2026-05-23 (template static-member initializer replay metadata invariants enforced)

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
- **deferred class-template constructor bodies now store `template_param_names`**
  so the replay pass sets `hasActiveTemplateParameters() = true`; and integral
  NTTPs referenced inside such bodies are now substituted directly to
  `NumericLiteralNode` during replay instead of falling through to a runtime
  `IdentifierNode` that codegen cannot resolve.
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

Latest recorded full-suite validation:
`2501` regular tests compiled/linked/runtime-pass, `0` fail, `181` expected-fail tests.

Latest focused replay regressions added on the current branch:
- `test_template_nested_ool_member_template_outer_param_binding_ret0.cpp`
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

## What is still wrong

### 1. Two-phase lookup and replay ownership are still incomplete

This is still the main conformance problem.

The remaining issue is not that replay exists. The issue is that some replay
paths still do not capture enough semantic metadata at parse time, so later
instantiation has to recover intent from partially substituted AST state.

The next highest-value remaining surface:

- remaining declaration replay paths outside static-member initializers that
  still recover intent from partially substituted AST state.
- pointer/reference/function-pointer NTTP substitution in deferred constructor
  bodies: these currently fall back to `IdentifierNode` and are resolved at
  instantiation time by `substitute_template_params_in_expression`; a dedicated
  path should handle them at parse time for uniformity.

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

1. **Remove the next AST-only replay fallback in declaration replay**
   - Continue with the remaining non-static declaration replay paths that still
     never captured enough metadata at parse time.
   - Prefer replay-first semantic attachment over adding more repair logic.

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
  outer parameter types while importing definition-side parameter names;
- partial-spec plain (non-template) out-of-line member functions now parse and
  substitute their bodies with the correct class-template parameters and
  definition lookup context in scope;
- partial-spec member-function-template instantiation now builds owner-correct
  nodes, registers qualified names, and attaches outer template bindings so the
  replay path can materialize bodies with the correct class-template parameters
  in scope.
- deferred class-template constructor bodies now store template-parameter names
  at parse time, so replay sets `hasActiveTemplateParameters() = true`; integral
  NTTPs referenced inside such bodies are substituted to `NumericLiteralNode`
  during replay, preventing "symbol not found" codegen failures.
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
