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
  `DependentUnqualifiedCallLookupRecord` when the instantiated target still
  matches the definition-bound ordinary lookup, covering replay-heavy member
  instantiations without relying on sema mangled-name recovery

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

### 1. Remaining non-definition-bound direct-call metadata loss

The definition-bound dependent-unqualified replay path is now preserved through
resolved-call materialization, but some instantiated ordinary direct calls can
still reach sema with only an authoritative mangled target when the final
point-of-instantiation result is not representable by the current preserved
record alone (for example, ADL-completed dependent-unqualified calls).

The current mangled-name recovery in sema remains an acceptable narrow
compatibility boundary there, but it is not the desired end state.

Desired end state:

- the instantiated call preserves enough structured semantic evidence to
  recover the final point-of-instantiation result directly
- sema does not need mangled-name recovery to stay definition-bound or to
  reuse a completed dependent-unqualified result
- the final parser-selected fallback in `resolveCallArgAnnotationTarget(...)`
  becomes removable

### 2. Residual replay/`StructTypeInfo` sync gaps

The broad signature-equivalent sync fallback is gone from the currently-covered
paths, but future failures in replay-heavy areas will likely still come from
metadata loss between source declarations and instantiated `StructTypeInfo`
copies.

Desired end state:

- every replay/sync path preserves source-member identity directly
- `StructTypeInfo` repair scans are not used to rediscover targets by name or
  arity when identity could have been preserved

### 3. Remaining semantic call compatibility fallback

The ordinary non-receiver direct-call path is much narrower now, but one final
parser-selected compatibility route still exists after typed lookup. That
fallback should only survive where replay/materialization still drops decisive
semantic evidence.

Desired end state:

- sema-owned typed lookup resolves or rejects ordinary direct calls directly
- parser-selected direct-call fallback disappears from normalized flows

### 4. Current-instantiation / unknown-specialization precision

This is no longer the first task, but it remains the next structural lever once
the direct-call and replay-identity gaps above are smaller. Expand it only
where it unblocks real replay or typed-lookup failures.

## Recommended task order

1. Extend the preserved direct-call metadata so non-definition-bound
   point-of-instantiation results (especially ADL-completed
   dependent-unqualified calls) survive replay/materialization without
   mangled-name recovery.

2. Remove the remaining final parser-selected non-receiver direct-call fallback
   in `resolveCallArgAnnotationTarget(...)` once step 1 proves the owning paths
   carry enough semantic evidence.

3. Fix the next replay/`StructTypeInfo` sync miss by preserving source identity
   in that path instead of restoring any signature-equivalent recovery logic.

4. Only after steps 1-3 are stable, expand
   current-instantiation/unknown-specialization modeling for the concrete cases
   that still block standards-conforming typed lookup.

## Validation guidance

When changing this area, always rerun:

- the focused regression that motivated the slice
- `template_lookup_non_dependent_no_rebind_ret0.cpp`
- `test_template_dependent_unqualified_mangled_recovery_ret0.cpp`
- `test_template_dependent_unqualified_member_replay_ret0.cpp`
- `test_template_qualified_direct_call_inner_return_overload_ret0.cpp`
- `test_template_dependent_unqualified_direct_call_nonviable_fail.cpp`
- `test_operator_subscript_sema_receiver_and_arg_overload_ret0.cpp`
- `test_operator_subscript_const_ambiguity_fail.cpp`
- `test_constexpr_operator_bracket_const_nonconst_ret0.cpp`
- `test_subscript_pointer_conversion_template_ret42.cpp`
- full `pwsh tests/run_all_tests.ps1` before closing the slice
