# Template Argument Architecture Audit

**Date:** 2026-05-12  
**Last updated:** 2026-06-11

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
- primary-template and covered specialization member-template copies now record
  `StructTypeInfo` identity at insertion time, so the remaining sync debt in
  this bucket is narrower: audit any still-uncovered constructor-template or
  replay-driven nested attachment path that reaches shared sync helpers without
  an identity index even after the matched source declaration is known

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

Desired end state:

- every instantiated ordinary direct call preserves enough structured semantic
  evidence to recover its final target directly
- sema does not need mangled-name recovery to reuse a completed
  replay/materialization result

### 3. Current-instantiation / unknown-specialization precision

This is no longer the first task, but it remains the next structural lever once
the replay-identity gaps above are smaller. Expand it only where it unblocks
real replay or typed-lookup failures.

## Recommended task order

1. Audit the remaining constructor-template / nested replay `StructTypeInfo`
   sync path that still reaches shared helpers without a preserved identity
   index, and close it the same way this slice closed the member-template
   copies.

2. Tighten the next remaining mangled-name compatibility path by preserving
   structured direct-call metadata in the owning replay/materialization path
   instead of relying on mangled recovery.

Next direct-call target:

- remove or narrow the remaining generic ordinary-call
  `lookupFunctionByMangledName(call_info.mangled_name)` fallback in sema by
  identifying which parser/materialization path still arrives there without a
  trustworthy structured direct-call target

3. Only after steps 1-2 are stable, expand
   current-instantiation/unknown-specialization modeling for the concrete cases
   that still block standards-conforming typed lookup.

## Validation guidance

When changing this area, always rerun:

- the focused regression that motivated the slice
- `test_template_ool_member_template_operator_identity_ret0.cpp`
- `template_lookup_non_dependent_no_rebind_ret0.cpp`
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

1. Inspect the remaining constructor-template and replay-only nested sync
   callers for any path that still reaches
   `findReplayedOutOfLineConstructorInStructInfo(...)` without a populated
   `SourceMemberStructInfoIndexMaps` entry after source matching succeeded.

2. After the replay/sync identity coverage is closed, remove or narrow the
   remaining generic ordinary-call
   `lookupFunctionByMangledName(call_info.mangled_name)` compatibility path in
   sema by preserving the structured direct-call target in the owning
   materialization path.

3. Only then spend complexity on current-instantiation /
   unknown-specialization expansion, and only for concrete typed-lookup or
   replay failures that remain after steps 1-2.
