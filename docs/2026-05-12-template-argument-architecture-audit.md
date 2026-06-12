# Template Argument Architecture Audit

**Date:** 2026-05-12  
**Last updated:** 2026-06-12

This document is a planning aid for the remaining template-infrastructure work.
It should describe the current architectural baseline, the highest-value
remaining gaps, and the next tasks to execute. It is intentionally not a
chronological work log.

## Current architectural baseline

The template system is now substantially more sema-owned than parser-owned in
the areas that were previously blocking standards-visible behavior:

- covered non-dependent template-body lookup preserves definition-context
  binding
- dependent alias resolution is semantic-only; the old textual
  `base::member` reconstruction path is gone
- replay/materialization paths are increasingly source-identity-driven instead
  of shape-driven
- normalized template/member direct-call resolution uses sema-owned
  overload-resolution argument typing rather than parser-owned argument typing
- semantic receiver-sensitive lookup for ordinary member calls, `operator[]`,
  and callable `operator()` now shares the same const-aware candidate
  partitioning model
- parser-selected direct/member-call targets are no longer trusted early once
  sema has enough typed evidence to decide or reject the call
- direct-call viability now requires real conversion paths instead of accepting
  parser-only optimistic struct-to-scalar matches
- function-pointer/function-reference call typing preserves the callee return
  type during overload ranking
- replay attachment in the covered out-of-line member and constructor paths now
  expects positive identity/signature evidence rather than silently accepting
  unresolved shape-based matches
- resolved direct-call materialization now preserves
  `DependentUnqualifiedCallLookupRecord` both when the instantiated target
  stays definition-bound and when dependent-unqualified POI completion selects
  a different final function, so sema/constexpr can reuse structured call
  metadata instead of re-running parser lookup or relying on mangled-name
  recovery in those paths
- non-dependent ordinary overload-resolution and namespace-qualified direct
  call materialization in the covered template/replay paths now preserve
  `FunctionCallDefinitionLookupRecord`, so sema no longer has to recover those
  definition-bound targets from `call_info.mangled_name`
- current-member-context static direct-call replay now preserves
  `FunctionCallDefinitionLookupRecord` in the
  `resolved_member_function_from_context` fast path, so later recovery does
  not have to rediscover those definition-bound member targets from
  `mangled_name`/`qualified_name` compatibility data alone
- builtin-like literal/pointer argument types no longer block
  `FunctionCallDefinitionLookupRecord` creation in that fast path just because
  parser-side `TypeSpecifierNode` materialization has not populated a concrete
  `type_index` yet
- the restored `int -> long` current-member static regression no longer relies
  on a per-argument codegen repair; sema now has a whole-call synchronization
  hook on the exact `CallExprNode` codegen lowers, and that hook is narrowly
  limited to definition-bound / qualified / static-member direct calls where
  the parser already preserves the selected target
- current-struct explicit member-template materialization now routes its
  qualified-name override through the shared direct-call metadata helper instead
  of stamping the qualified name separately before attaching the rest of the
  call metadata
- the shared qualified member-template call helper now also preserves
  `FunctionCallDefinitionLookupRecord` when it has a concrete direct-call
  target plus typed arguments, instead of carrying only
  `qualified_name`/`mangled_name` compatibility metadata
- the final parser-selected non-receiver direct-call fallback in
  `resolveCallArgAnnotationTarget(...)` is gone; ordinary direct calls now
  resolve through preserved semantic metadata, typed lookup, or explicit
  unresolved terminals instead of reusing the parser-selected target late
- primary-template out-of-line constructor replay now synchronizes the
  `StructTypeInfo` constructor copy through preserved source-member identity
  when that identity is already known, instead of always rescanning
  `StructTypeInfo` constructors afterward
- copied member-function-template stubs now register source-member identity
  into `StructTypeInfo` immediately in the primary-template and covered
  specialization paths, including operator overloads, so later out-of-line
  replay sync no longer has to recover those copies through replay-source-key
  scans once the matched source declaration is already known
- nested class-template member-function population now happens before
  nested out-of-line replay, with `StructTypeInfo` identity recorded at
  insertion time and lazy-registration split from structural insertion, so the
  nested constructor/member-template replay helpers no longer depend on
  positional back-fill of `StructTypeInfo` indices in that path

## Architectural invariants to preserve

Follow-up work in this area should preserve these rules:

- preserve semantic identity end-to-end; do not rebuild meaning from text if a
  structured record can be carried instead
- prefer replay-first attachment using source identity plus canonical
  substituted-signature evidence
- treat parser-selected compatibility paths as temporary debt, not as a normal
  resolution mechanism
- when sema has enough typed evidence to decide, reject broad parser/codegen
  fallback instead of letting it continue
- if a path still lacks enough metadata, narrow the compatibility surface and
  document the gap explicitly

## Highest-value remaining architectural gaps

### 1. Residual replay/`StructTypeInfo` sync gaps

The broad signature-equivalent sync fallback is gone from the currently-covered
paths, but future failures in replay-heavy areas will likely still come from
metadata loss between source declarations and instantiated `StructTypeInfo`
copies.

Desired end state:

- every replay/sync path preserves source-member identity directly
- `StructTypeInfo` repair scans are not used to rediscover targets by name or
  arity when identity could have been preserved

Near-term remaining scope:

- in-loop member-function / constructor registration inside
  `Parser::try_instantiate_class_template()` is now unified locally, so the
  next cleanup target in this area is the remaining out-of-line replay/sync
  helper duplication rather than more registration drift
- top-level replay-driven `StructTypeInfo` sync now uses shared
  identity-first helpers for plain members and constructors; the remaining
  duplication is in nested/member-template-specific attachment paths that do
  not all route through those helpers yet
- partial-specialization constructor copies now use the same
  source-member-to-`StructTypeInfo` index map as the primary-template path,
  and the nested/member-template replay sites that already preserve a matched
  source declaration now route through the shared identity-first helpers too
- partial-specialization nested member-template replay now also syncs the
  `StructTypeInfo` copy through the shared identity-first helper once the
  matched source declaration is preserved
- the previously highest-impact nested replay/sync gap is now covered in the
  primary nested-class path as well; remaining replay debt in this bucket is no
  longer the obvious first task and should be handled opportunistically only if
  a concrete uncovered attachment/sync failure appears

### 2. Remaining direct-call metadata loss outside dependent-unqualified completion

Dependent-unqualified direct-call completion now preserves both the
definition-bound ordinary lookup and the final point-of-instantiation target
through resolved-call materialization, and the final parser-selected
non-receiver fallback is gone. The remaining direct-call metadata risk is
therefore narrower: other instantiated ordinary direct-call paths may still
reach sema with only mangled compatibility evidence instead of preserved
structured lookup metadata.

Latest progress:

- sema now prefers the structured `FunctionDeclarationNode*` already stored in
  definition-lookup and dependent-unqualified records, and only falls back to
  mangled-name canonicalization when that structured target is absent
- the highest-traffic non-dependent ordinary direct-call branches now preserve
  `FunctionCallDefinitionLookupRecord` instead of only stamping
  `mangled_name`, including unqualified overload-resolution paths in template
  bodies and namespace-qualified direct-call materialization

Desired end state:

- every instantiated ordinary direct call preserves enough structured semantic
  evidence to recover its final target directly
- sema does not need mangled-name recovery to reuse a completed
  replay/materialization result

Remaining near-term scope:

- the remaining compatibility surface is now concentrated in parser paths that
  still stamp only `mangled_name` or `qualified_name` without a typed
  definition-lookup record, especially qualified-member materializers,
  untyped fallback branches, some qualified member-template call materializers,
  and the user-defined literal operator path
- the remaining sema/codegen boundary sync for direct calls should stay narrow;
  it now operates at whole-call granularity on the exact lowered AST, but the
  long-term target is still to remove even that hook once the remaining
  replay/materialization paths preserve enough structured call identity that
  sema never has to resynchronize the lowered node

### 3. Current-instantiation / unknown-specialization precision

This is no longer the first task, but it remains the next structural lever once
the replay-identity gaps above are smaller. Expand it only where it unblocks
real replay or typed-lookup failures.

## Recommended task order

1. Tighten the next remaining mangled-name compatibility path by preserving
   structured direct-call metadata in the owning replay/materialization path
   instead of relying on mangled recovery.

Next direct-call target:

- trace the remaining `Parser_Expr_PrimaryExpr.cpp` direct-call sites that
  still attach only compatibility metadata, then either preserve a typed
  `FunctionCallDefinitionLookupRecord` there or explicitly document why the
  call cannot yet carry one; after the current-member-context fast path, the
  next typed branch to tighten is the remaining manual qualified-member
  materializer coverage that still bypasses the shared helper

2. Only after step 1 is stable, expand
   current-instantiation/unknown-specialization modeling for the concrete cases
   that still block standards-conforming typed lookup.

## Validation guidance

When changing this area, always rerun:

- the focused regression that motivated the slice
- `test_template_ool_member_template_operator_identity_ret0.cpp`
- `test_template_nested_ool_ctor_template_same_name_overload_ret0.cpp`
- `test_template_nested_ool_ctor_template_outer_inner_param_rename_ret42.cpp`
- `test_template_nested_ool_ctor_template_init_replay_ret42.cpp`
- `template_lookup_non_dependent_no_rebind_ret0.cpp`
- `test_template_explicit_function_id_definition_bound_ret0.cpp`
- `test_template_current_member_static_hides_base_overload_ret0.cpp`
- `test_template_current_member_static_hides_base_enum_conversion_ret0.cpp`
- `test_template_qualified_member_template_hides_base_overload_ret0.cpp`
- `test_template_dependent_unqualified_mangled_recovery_ret0.cpp`
- `test_template_dependent_unqualified_member_replay_ret0.cpp`
- `test_template_dependent_unqualified_poi_adl_record_ret42.cpp`
- `test_template_qualified_direct_call_inner_return_overload_ret0.cpp`
- `test_template_dependent_unqualified_direct_call_nonviable_fail.cpp`
- `test_operator_subscript_sema_receiver_and_arg_overload_ret0.cpp`
- `test_operator_subscript_const_ambiguity_fail.cpp`
- `test_constexpr_operator_bracket_const_nonconst_ret0.cpp`
- `test_subscript_pointer_conversion_template_ret42.cpp`
- full `pwsh tests/run_all_tests.ps1` before closing the slice

## Next steps

1. Finish the remaining parser-side direct-call metadata preservation in the
   still-uncovered resolved-call paths that only stamp `mangled_name` or
   `qualified_name`, starting with the remaining typed qualified-member
   materializers and then the untyped fallback branches.
   Immediate follow-up: convert the remaining manual compatibility-only
   qualified-member materializer branches in `Parser_Expr_PrimaryExpr.cpp`
   that still bypass the shared helper to preserve
   `FunctionCallDefinitionLookupRecord`, then move on to the untyped fallback
   branches. Keep using focused regressions where hidden or later same-name
   overloads could otherwise steal replayed call targets. After that, collapse
   the new whole-call sema synchronization hook by proving those paths carry
   their conversion annotations before lowering.

2. Only then spend complexity on current-instantiation /
   unknown-specialization expansion, and only for concrete typed-lookup or
   replay failures that remain after step 1.
