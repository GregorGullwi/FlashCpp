# Template Argument Standard-Conformance Investigation

**Date:** 2026-05-12  
**Last updated:** 2026-06-12

This document tracks the standards-facing target for the remaining template
infrastructure work. It should describe the intended semantic model, the
highest-value remaining conformance gaps, and the next tasks required to close
them. It is intentionally not a branch diary.

## Target model

Move FlashCpp toward a template implementation where:

- dependency classification is explicit
- non-dependent lookup remains definition-bound through instantiation
- dependent names preserve semantic identity end-to-end
- substitution, deduction, ranking, and replay/materialization stay separate
- semantic analysis, not parser leftovers, owns final call-target selection in
  normalized flows
- replay succeeds because invariant evidence matches, not because a repair scan
  guessed correctly

## Current conformance baseline

The compiler now has a workable standards-facing baseline in several previously
blocking areas:

- covered non-dependent template-body lookup remains definition-bound
- dependent alias resolution no longer relies on textual reconstruction
- normalized direct-call resolution for template/member calls uses sema-owned
  overload-resolution argument typing
- semantic const-aware lookup for ordinary member calls, `operator[]`, and
  callable `operator()` now rejects stale non-const parser-selected targets
  once sema has typed evidence
- direct-call viability now requires real conversion paths
- indirect call typing preserves the returned object type during overload
  ranking
- replay attachment in the covered routes now expects positive
  identity/signature evidence rather than accepting unresolved
  shape-based matches
- resolved direct-call materialization now preserves
  `DependentUnqualifiedCallLookupRecord` both when the instantiated target
  stays definition-bound and when dependent-unqualified POI completion selects
  a different final function, reducing replay-heavy dependence on parser
  reruns and mangled-name recovery in those paths
- non-dependent ordinary overload-resolution and namespace-qualified direct
  call materialization in the covered template paths now also preserve
  `FunctionCallDefinitionLookupRecord`, reducing the remaining standards risk
  that sema will reconstruct definition-bound targets from mangled names alone
- current-member-context static direct-call replay now also preserves
  `FunctionCallDefinitionLookupRecord` in the
  `resolved_member_function_from_context` fast path, so sema no longer has to
  recover those definition-bound member targets from compatibility metadata
  alone in that route
- builtin-like literal/pointer argument types no longer prevent that fast path
  from carrying `FunctionCallDefinitionLookupRecord` just because the parser
  has not yet materialized a concrete `type_index` for the temporary
  `TypeSpecifierNode`
- the restored `int -> long` current-member static case no longer relies on a
  per-argument codegen patch; sema now synchronizes the exact lowered
  `CallExprNode` at whole-call granularity, and that retry is narrowly bounded
  to definition-bound / qualified / static-member direct calls whose selected
  target is already preserved on the AST
- current-struct explicit member-template materialization now routes its
  qualified-name override through the same shared direct-call metadata helper
  that attaches the rest of the structured call metadata
- the shared qualified member-template call helper now also preserves
  `FunctionCallDefinitionLookupRecord` when it has a concrete direct-call
  target plus typed arguments, instead of falling back to
  `qualified_name`/`mangled_name` compatibility metadata alone
- the final parser-selected non-receiver direct-call fallback in
  `resolveCallArgAnnotationTarget(...)` is gone; ordinary direct calls now
  resolve through semantic metadata, typed lookup, or explicit unresolved
  terminals instead of reusing the parser-selected target late
- ordinary direct-call parsing now performs one last structured
  `appendFunctionCallArgType(...)` collection pass before falling back to
  compatibility-only metadata when primary `get_expression_type(...)` typing
  fails, reducing the remaining surface where parser materialization lag alone
  strips away definition-bound call identity
- string-literal user-defined literal calls now preserve
  `FunctionCallDefinitionLookupRecord` using the synthesized literal-operator
  name and argument list instead of carrying only `mangled_name`
- primary-template out-of-line constructor replay now synchronizes the
  `StructTypeInfo` constructor copy through preserved source-member identity
  when that identity is already known, instead of recovering it afterward
- copied member-function-template stubs now preserve source-member identity in
  `StructTypeInfo` at insertion time in the primary-template and covered
  specialization paths, including operator overloads, so out-of-line replay
  sync no longer depends on replay-source-key recovery there once semantic
  matching already identified the source declaration
- nested class-template member functions are now inserted into
  `StructTypeInfo` before nested out-of-line replay with identity recorded at
  insertion time, and lazy nested-member registration is split from that
  structural insertion so nested constructor/member-template replay no longer
  depends on positional `StructTypeInfo` back-filling in the covered path

## Highest-value remaining standards gaps

### 1. Replay must stay invariant-driven

The remaining standards risk is no longer textual dependent-name recovery. It
is replay/materialization paths that may still succeed or sync through weak
name/arity evidence instead of source identity plus canonical substituted
signatures.

Why this matters:

- replay that succeeds through shape-based repair can silently select the wrong
  declaration
- those weak paths tend to reopen non-conforming fallback behavior later in
  sema or codegen

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
- the previously highest-value replay/sync identity gaps for covered
  member-template and nested constructor-template replay are now closed; any
  remaining work in this bucket should be driven by a concrete uncovered
  attachment failure rather than treated as the default next slice

### 2. Compatibility recovery is still covering direct-call metadata loss outside dependent-unqualified completion

Dependent-unqualified replay/materialization now preserves both the
definition-bound ordinary lookup and the final point-of-instantiation target.
The remaining compatibility risk is therefore narrower: other instantiated
ordinary direct calls can still lose structured semantic evidence and fall back
to mangled-name recovery instead of carrying their final lookup result
directly.

Latest progress:

- sema now consumes the structured `FunctionDeclarationNode*` already stored in
  definition-lookup and dependent-unqualified records before considering
  mangled-name canonicalization
- the highest-traffic non-dependent ordinary direct-call branches now preserve
  `FunctionCallDefinitionLookupRecord` directly, including unqualified
  overload-resolution in template bodies and namespace-qualified direct calls

Why this matters:

- a preserved mangled target is only a compatibility boundary
- the real semantic model should still know why the call is definition-bound
  and, when POI completion changes the winner, which final semantic target was
  selected

Remaining near-term scope:

- the remaining compatibility boundary is now concentrated in parser
  materialization paths that still carry only `mangled_name` or
  `qualified_name`, especially the fallback exits that still cannot assemble
  stable typed arguments even after the new structured retry pass, plus any
  remaining niche qualified/member-template materializers that do not yet
  route through the shared helpers
- the remaining whole-call sema synchronization hook for direct calls should
  remain temporary; the remaining parser/materialization work should make even
  that narrowed retry unnecessary by ensuring the structured call path already
  owns the needed target and argument-conversion state

### 3. Current-instantiation / unknown-specialization coverage

This is still necessary, but it is no longer the immediate next task. Expand
it only for concrete unresolved cases that block steps 1-2.

## Priority order

1. Tighten the next remaining mangled-name compatibility path by preserving
   structured direct-call metadata in the owning replay/materialization path
   instead of relying on mangled recovery.

Next direct-call target:

- identify the remaining direct-call parser/materialization sites that still
  reach sema with compatibility-only metadata, then replace each typed case
  with preserved structured target metadata before touching the sema fallback;
  after the now-covered typed qualified-member and string-literal UDL paths,
  the next target is the final fallback exits that still cannot produce stable
  typed arguments even after the structured retry

2. Expand current-instantiation and unknown-specialization handling only where
   it unblocks concrete replay or typed-lookup failures still remaining after
   step 1.

## Standards rules for follow-up work

- do not normalize textual reconstruction as acceptable semantics
- prefer hard failure or proper diagnostics over silent repair in normalized
  semantic flows
- keep compatibility behavior explicit, narrow, and temporary
- treat codegen-side recovery as debt to remove, not as architecture
- when a late retry is still necessary, route it through typed semantic lookup
  instead of rebuilding intent from strings or shallow shape matches

## Validation guidance

For work in this area, rerun:

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
- full `pwsh tests/run_all_tests.ps1` before considering the slice complete

## Next steps

1. Continue eliminating compatibility-only parser metadata in the remaining
   resolved direct-call sites, now focusing on the branches that still cannot
   produce stable typed arguments even after the structured retry pass.
   Immediate follow-up: audit the last compatibility-only fallback exits in
   `Parser_Expr_PrimaryExpr.cpp` and either preserve
   `FunctionCallDefinitionLookupRecord` there or reclassify those calls as
   unresolved/deferred instead of pretending they are fully resolved. Keep
   adding focused regressions for replayed calls that would otherwise be
   vulnerable to hidden or later same-name overloads. Once those paths are
   covered, remove the new whole-call sema synchronization hook by pushing that
   work back to earlier semantic ownership.

2. Use any concrete failures left after step 1 to drive the next
   current-instantiation / unknown-specialization expansion rather than
   widening that model preemptively.
