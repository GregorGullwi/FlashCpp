# Template Argument Architecture Audit

**Date:** 2026-05-12  
**Last updated:** 2026-06-14

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
- ordinary direct-call parsing now makes one last structured
  `appendFunctionCallArgType(...)` pass before dropping to the
  compatibility-only fallback when primary `get_expression_type(...)`
  collection fails, shrinking the cases that still lose
  `FunctionCallDefinitionLookupRecord` just because parser-side type
  materialization lagged behind
- that structured retry is now shared with the current-member static fast
  path as well, so those replay-preserving calls do not regress into a
  separate weaker argument-typing path
- string-literal user-defined literal calls now preserve
  `FunctionCallDefinitionLookupRecord` using the synthesized literal-operator
  name and argument list, instead of carrying only `mangled_name`
- the remaining untyped ordinary direct-call fallback exits no longer keep a
  parser-selected callee as the call's semantic identity just to preserve
  parse-time typing; they now carry an explicit parser return-type hint plus
  either a deferred definition-bound lookup record or the existing deferred
  dependent-unqualified lookup record, and sema can resolve those calls later
  from structured lookup state instead of mangled-name recovery
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
- parser-side pack-expansion handling for ordinary identifier-call and shared
  argument-list parsing no longer treats "no matching function-parameter pack
  found" as a successful expansion; unmatched complex expansions now stay as
  `PackExpansionExprNode` until substitution, and call-site substitution can
  scalarize the active pack bindings element-by-element instead of silently
  flattening `expr...` into a single ordinary argument
- deferred qualified/member-template direct calls now preserve explicit
  template-argument AST, parser return-type hints, and dependent-qualified
  lookup records separately instead of collapsing semantic ownership onto a
  parser-selected callee
- qualified direct-call target resolution now distinguishes type owners from
  namespace qualifiers before consuming definition-bound compatibility data, so
  sema resolves nested current-instantiation owners structurally and no longer
  lets unrelated global templates steal calls like `Ops::read(value)` inside
  `Runner<T>`
- explicit qualified member-template direct calls now preserve nested current-
  instantiation owner identity in the parser/materialization layer when the
  owner spelling names a real nested owner (for example `Ops::template read<int>(value)`
  inside `Runner<T>`), instead of normalizing through the standalone owner
  spelling and letting an unrelated global template owner win
- that owner recovery now walks enclosing nested class/member-function contexts
  from inner to outer, so the same explicit qualified member-template path also
  resolves owners declared on an enclosing current instantiation from inside a
  nested class body
- that parser-side owner rewrite is now intentionally narrow: it only rewrites
  short owner spellings when the resolved owner is a true nested owner
  extension (for example `Runner<int>::Ops`), so alias/self-owner qualified
  calls remain on their existing paths instead of being eagerly collapsed onto
  unrelated concrete owner names

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

- the highest-impact legacy unified ordinary identifier-call fallback is now on
  the proper split model: parse-time return-type visibility lives on an
  explicit call-node hint, while deferred semantic ownership lives on a
  definition-bound or dependent-unqualified lookup record rather than on a
  parser-selected callee
- the remaining debt is narrower and now concentrated in other parser
  materialization sites that still stamp only `mangled_name` or
  `qualified_name` without a typed semantic record, especially niche
  qualified/member-template paths outside the now-covered ordinary-call routes
- the newly-covered qualified-owner split is intentionally sema-owned: the
  deferred-template resolver is now bypassed for non-template qualified calls
  whose left-hand side is a real type owner, but the parser helper still has a
  compatibility-only ordinary-static-member branch that should eventually be
  folded into the same structured owner/lookup model instead of remaining a
  side path
- the previously uncovered `typeCode<Rest>()...`-style call-argument leak is
  now fixed at the parser/substitution boundary; future work here should keep
  pack-expansion ownership at that boundary instead of reintroducing
  call-specific explicit-template-argument repairs deeper in sema/codegen
- the standards-visible nested-owner collision in explicit qualified
  member-template calls is now fixed in the parser/materialization layer by
  preserving the full nested owner identity instead of recovering from the
  short owner spelling later; remaining work in this bucket is the still-legacy
  compatibility branch for ordinary static-member fallback inside
  `resolveDeferredQualifiedTemplateCall(...)`, not more owner-collision repair
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
  call cannot yet carry one; after the current-member-context fast path and
  the untyped ordinary-call deferred-lookup split, the next highest-value
  targets are the remaining qualified/member-template materializers that still
  carry only `qualified_name`/`mangled_name` compatibility data
- before expanding sema-side owner recovery further, fix the remaining
  standards-visible nested-owner collision in explicit qualified member-template
  calls where a nested owner name inside the current instantiation collides with
  an unrelated global template owner
  status: covered by the new parser-side nested-owner identity preservation;
  keep the focused regression for this case in the guard set and do not widen
  the rewrite past true nested-owner extensions

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
- `test_template_qualified_member_template_nested_owner_collision_ret0.cpp`
- `test_template_qualified_member_template_nested_owner_chain_collision_ret0.cpp`
- `test_template_qualified_member_template_enclosing_owner_collision_ret0.cpp`
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
   `qualified_name`, now focusing on qualified/member-template materializers
   outside the now-covered ordinary identifier-call routes.
   Immediate follow-up: move those paths onto the same split ownership model
   now used by untyped ordinary calls:
   parser-time return-type hints on the call node, structured deferred lookup
   state for semantic ownership, and no parser-selected callee as the source of
   final meaning. First target: remove the remaining ordinary-static-member
   compatibility branch inside `resolveDeferredQualifiedTemplateCall(...)` by
   routing those cases through the same sema-owned type-owner vs
   namespace-qualifier split now used in
   `resolveCallArgAnnotationTarget(...)`. After that, collapse the new
   whole-call sema synchronization hook by proving those paths carry their
   conversion annotations before lowering. Also clean up the remaining legacy parser sites that still
   hand-roll `expr...` handling (constructor/initializer parsing) so they share
   the same "expand only when a real function pack matched, otherwise preserve
   the pack node" rule now enforced in ordinary call parsing.
   The nested-owner explicit member-template collision is now fixed at the
   parser source and guarded by
   `test_template_qualified_member_template_nested_owner_collision_ret0.cpp`
   plus the multi-segment owner-chain variant
   `test_template_qualified_member_template_nested_owner_chain_collision_ret0.cpp`
   and the enclosing-scope nested-class variant
   `test_template_qualified_member_template_enclosing_owner_collision_ret0.cpp`.
   The next concrete target in this slice is therefore the remaining
   ordinary-static-member compatibility branch inside
   `resolveDeferredQualifiedTemplateCall(...)`: move that branch onto the same
   structured type-owner vs namespace-qualifier split, then remove any now-
   redundant whole-call sema resynchronization that was only compensating for
   that compatibility path. Long-term direction: stop teaching individual
   parser call sites to recover owner identity from short qualified spellings.
   Instead, preserve a shared structured qualified-owner record on the AST and
   route both parser materialization and sema fallback through one owner/type-
   owner-vs-namespace resolver so this pattern does not repeat in more
   qualified-call subpaths.

2. Only then spend complexity on current-instantiation /
   unknown-specialization expansion, and only for concrete typed-lookup or
   replay failures that remain after step 1.
