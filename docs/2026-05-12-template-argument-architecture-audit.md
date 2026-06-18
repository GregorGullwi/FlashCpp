# Template Argument Architecture Audit

**Date:** 2026-05-12  
**Last updated:** 2026-06-15

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
- `resolveDeferredQualifiedTemplateCall(...)` no longer performs ordinary
  static-member overload recovery for qualified type owners; those non-template
  direct calls now stay on sema-owned owner/overload resolution instead of
  reusing the parser's compatibility path
- constexpr evaluation now reuses sema-owned qualified direct-call resolution
  before falling back to parser-owned deferred template instantiation, and that
  shared resolver accepts an explicit current-type fallback so class-scope
  `static constexpr` initializers in the current instantiation keep the same
  nested-owner/type-owner-vs-namespace split that member-function sema already
  used
- instantiation-time resolved-call materialization in `ExpressionSubstitutor`
  now opportunistically rebuilds `FunctionCallDefinitionLookupRecord` for
  qualified/member-template direct calls once substitution has concrete
  arguments plus a concrete `FunctionDeclarationNode`, instead of preserving
  only copied `qualified_name`/`mangled_name` compatibility metadata in those
  paths
- postfix qualified direct-call and concrete member-postfix fast paths in
  `Parser_Expr_PostfixCalls.cpp` now attach structured call metadata when they
  already hold a concrete `FunctionDeclarationNode`, instead of stopping at
  copied `qualified_name`/`mangled_name` compatibility data in those parser
  materializers
- direct operator function-id calls in `Parser_Expr_PrimaryExpr.cpp` and the
  postfix declaration-address fast path in `Parser_Expr_PostfixCalls.cpp` now
  preserve structured non-receiver call metadata as well, so late sema no
  longer has to rediscover their definition-bound target from a parser-
  selected callee alone
- the remaining concrete receiver-call builders now preserve the same shared
  metadata too: postfix `operator()` finalization plus the implicit-`this`,
  member-operator, and callable-object `operator()` builders in
  `Parser_Expr_PrimaryExpr.cpp` all attach structured receiver-call metadata
  once they already hold the exact `FunctionDeclarationNode`
- template-parameter function-pointer direct-call materialization in
  `Parser_Expr_PrimaryExpr.cpp` now preserves
  `FunctionCallDefinitionLookupRecord` when definition-context lookup already
  resolves the bound function, so later sema does not have to rediscover that
  target from name compatibility after additional same-name overloads appear
- explicit-qualified postfix member/operator call builders in
  `Parser_Expr_PostfixCalls.cpp` now resolve concrete owner-qualified targets
  for forms such as `this->Base::touch()` and `this->Base::operator()()` when
  the receiver type and arguments are already concrete, instead of falling back
  to placeholder `auto` member-call nodes that later lose semantic identity
- the shared qualified-owner member-call helper in
  `Parser_Expr_QualLookup.cpp` now treats concrete `Type::member(...)` owner
  calls more semantically too: non-template fallback selection is limited to
  static members, uses real overload resolution once argument types are
  concrete, and unresolved template-instantiation owners preserve a structured
  dependent-qualified owner record even when `StructTypeInfo` is already
  present instead of dropping back to `qualified_name` plus a parser return-type
  hint alone
- the unqualified/current-owner explicit-member-template placeholder path in
  `Parser_Expr_PrimaryExpr.cpp` now preserves that same split for deferred
  class-scope member-template calls: it keeps a structured current-
  instantiation `DependentQualifiedNameRecord`, and parser return-type hints
  now come from the matched current-owner member-template declaration instead
  of whichever unrelated same-name global template was encountered first
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
- deferred namespace-qualified explicit-template direct calls now also preserve
  definition-bound replay metadata when the visible template candidate is
  unique, but that exact parse-time binding is intentionally suppressed when
  the template name already has registered exact specializations so later
  instantiation can still select the specialization-first C++20 path
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
- instantiated template class members now preserve function-pointer member
  signatures through alias/template-argument recovery plus source
  `StructTypeInfo` fallback, so indirect calls through concrete
  function-pointer data members no longer lose the signature required for
  IR-lowering return typing
- postfix member-call placeholders for function-pointer data members now carry
  the synthesized return type from the member signature instead of a fake
  scalar stub, so overload resolution and chained postfix analysis no longer
  misclassify calls like `pick(this->callback())` inside template bodies
- `.*` / `->*` member-function-pointer postfix calls now preserve the pointed-
  to return shape and parser return-type hint through template-body
  materialization, so overload ranking and chained postfix analysis no longer
  collapse those calls onto fake scalar placeholders
- member-function-pointer template arguments now preserve declaring-class
  identity through substitution/materialization, MSVC mangling now covers
  member-function-pointer cv/ref/noexcept qualifiers plus ordinary
  pointer-to-member declarator forms, and the `_Is_memfunptr`-style partial-
  specialization matcher now deduces the owner class instead of treating the
  member-pointer owner as opaque text

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
  qualified/member-template paths outside the now-covered ordinary-call routes;
  the highest-impact postfix qualified/member fast paths plus the direct
  operator/declaration-address non-receiver paths are now covered too, and the
  main concrete receiver-call builders plus template-parameter
  function-pointer call materialization are covered as well, and the explicit-
  qualified postfix placeholder member/operator builders are now covered too,
  and ordinary function-pointer member postfix placeholders plus the
  `.*` / `->*` member-function-pointer fast paths now preserve the real return
  shape, and the shared qualified static-owner helper no longer relies on a
  same-arity shape match for its non-template fallback or drops structured
  owner metadata just because the owner `StructTypeInfo` is already present, so
  the remaining work is no longer that whole pointer-to-member
  surface; it is the smaller set of still-uncovered placeholder/materializer
  exits plus the canonical `TypeCategory::MemberObjectPointer` carrier gap
  where the underlying member type is not yet preserved strongly enough for
  every ABI-sensitive consumer
- the newly-covered qualified-owner split is intentionally sema-owned: the
  deferred-template resolver is now bypassed for non-template qualified calls
  whose left-hand side is a real type owner, and the old parser-side ordinary
  static-member recovery in `resolveDeferredQualifiedTemplateCall(...)` is now
  gone; the remaining debt in this sub-area is no longer owner-kind
  classification itself, but the unresolved qualified/member-template
  materializers that still preserve only compatibility metadata after the owner
  split has already been decided
- the now-covered unqualified/current-owner explicit-member-template path
  confirms the same rule for short class-scope spellings: deferred
  `pick<int>(...)`-style member-template calls inside a current instantiation
  must preserve structured owner/member identity and may not borrow parser-time
  return-type visibility from an unrelated `lookupAllTemplates(...)` result
- the generic namespace-qualified explicit-template placeholder/materializers
  in `Parser_Expr_PrimaryExpr.cpp` are now on that split model too, including a
  specialization-aware guard that only preserves exact parse-time template
  identity when no exact specialization is registered for the same qualified
  template name; the remaining work in this sub-area is therefore the other
  qualified/member-template parser exits, especially postfix explicit-qualified
  member/operator placeholder paths that still do not preserve the same
  deferred lookup structure end-to-end
- postfix explicit-qualified member/operator call parsing in
  `Parser_Expr_PostfixCalls.cpp` now also accepts explicit member-template ids
  after a concrete owner qualifier such as `this->Base::template pick<int>()`
  and `this->Base::template operator()<int>()`, routing the resolved owner
  through the same member-template instantiation helper and preserving
  `qualified_name + template_arguments` on unresolved placeholders so
  substitution can recover the base-qualified owner later
- the previously uncovered `typeCode<Rest>()...`-style call-argument leak is
  now fixed at the parser/substitution boundary; future work here should keep
  pack-expansion ownership at that boundary instead of reintroducing
  call-specific explicit-template-argument repairs deeper in sema/codegen
- the standards-visible nested-owner collision in explicit qualified
  member-template calls is now fixed in the parser/materialization layer by
  preserving the full nested owner identity instead of recovering from the
  short owner spelling later; the old ordinary static-member compatibility
  branch in `resolveDeferredQualifiedTemplateCall(...)` is now gone too, and
  the main postfix qualified/member parser path now preserves structured call
  metadata as well, so the remaining work in this bucket is the smaller set of
  substitution/parser materializers that still stop at compatibility data
  after the owner split has already succeeded
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

- trace the remaining concrete resolved-call builders outside the now-covered
  postfix qualified/member helpers, then either preserve a typed
  `FunctionCallDefinitionLookupRecord` there or explicitly document why the
  call cannot yet carry one; after the current-member-context fast path, the
  untyped ordinary-call deferred-lookup split, the postfix qualified/member
  metadata preservation pass, the direct operator/declaration-address
  metadata preservation pass, the removal of the parser-owned
  ordinary-static-member qualified fallback, and the substitution-time
  qualified-call record rebuild, template-parameter function-pointer call
  preservation, the `.*` / `->*` member-function-pointer pass, and
  explicit-qualified postfix member/operator preservation, the next
  highest-value targets are the remaining qualified/member-template
  compatibility-only materializers plus any smaller placeholder call builders
  that still return immediately without a structured lookup record. The shared
  qualified static-owner helper is no longer one of those weak spots: its
  non-template fallback now routes through real static-member overload
  selection, and unresolved template-instantiation owners keep structured
  owner metadata even when the owner `StructTypeInfo` is already available

2. Only after step 1 is stable, expand
   current-instantiation/unknown-specialization modeling for the concrete cases
   that still block standards-conforming typed lookup.

## Next steps

1. Finish the remaining parser/materialization sites that still preserve only
   `qualified_name` / `mangled_name` after owner resolution already succeeded,
   and move them onto the same deferred lookup + parser return-type-hint split
   now used by the covered ordinary-call paths.
   The postfix explicit-qualified member/operator owner-template-id exits in
   `Parser_Expr_PostfixCalls.cpp` are now covered too: `parse_member_postfix()`
   consumes qualifiers like `this->Base<T>::template pick<int>()` and
   `this->Base<T>::template operator()<int>()`, preserving dependent owner
   template-id structure through the same structured deferred qualified-call
   record used by the other qualified/member-template paths instead of dropping
   the `Base<T>` template-id before the `::template` continuation.
   Immediate next target: audit the remaining qualified/member-template
   placeholder/materializer exits in `Parser_Expr_PrimaryExpr.cpp` and
   `Parser_Expr_PostfixCalls.cpp` that still stop at compatibility metadata
   after owner resolution or deferred lookup shape is already known, and move
   them onto preserved `FunctionCallDefinitionLookupRecord` or explicit
   `DependentQualifiedNameRecord` state before removing the whole-call sema
   synchronization hook.
2. If another pointer-to-member issue appears, close the remaining canonical
   `TypeCategory::MemberObjectPointer` carrier gap by preserving the
   underlying member type explicitly instead of relying on declarator-shaped
   `member_class + pointer_depth` forms in ABI-sensitive paths.
3. Leave replay/`StructTypeInfo` sync and broader
   current-instantiation/unknown-specialization expansion in opportunistic mode
   unless a concrete uncovered failure appears.

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
- `test_template_qualified_static_member_same_arity_decltype_ret0.cpp`
- `test_template_current_member_explicit_template_decltype_ret0.cpp`
- `test_template_disambiguation_pack_ret40.cpp`
- `test_template_qualified_member_template_nested_owner_collision_ret0.cpp`
- `test_template_qualified_member_template_nested_owner_chain_collision_ret0.cpp`
- `test_template_qualified_member_template_enclosing_owner_collision_ret0.cpp`
- `test_template_qualified_namespace_explicit_definition_bound_ret0.cpp`
- `test_template_global_qualified_namespace_explicit_definition_bound_ret0.cpp`
- `test_template_qualified_namespace_explicit_runtime_definition_bound_ret0.cpp`
- `test_template_explicit_base_member_template_call_default_arg_ret0.cpp`
- `test_template_explicit_base_operator_template_call_default_arg_ret0.cpp`
- `test_template_explicit_base_owner_template_member_template_call_default_arg_ret0.cpp`
- `test_template_explicit_base_owner_template_operator_template_call_default_arg_ret0.cpp`
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
   final meaning. The old ordinary-static-member compatibility branch inside
   `resolveDeferredQualifiedTemplateCall(...)` is now removed, constexpr
   reuse goes through the same sema-owned type-owner vs namespace-qualifier
   split with explicit current-type fallback for class-scope evaluation, and
   the substitution-time qualified/member-template materializer now rebuilds
   `FunctionCallDefinitionLookupRecord` once the substituted call is concrete.
   The next concrete target in this slice is therefore narrower:
   audit the remaining parser materializers that still stamp only
   `qualified_name`/`mangled_name`, preserve
   `FunctionCallDefinitionLookupRecord` (or an explicit deferred lookup record)
   there, and then collapse the whole-call sema synchronization hook that was
   compensating for those compatibility-only paths. The just-finished
   `Parser_Expr_QualLookup.cpp` pass removed the weak same-arity/static-member
   fallback from `try_parse_member_template_function_call(...)` and widened
   dependent-qualified owner preservation for unresolved template-instantiation
   owners, and the current-owner explicit-member-template placeholder path in
   `Parser_Expr_PrimaryExpr.cpp` now preserves the same structured deferred
   owner metadata with member-template-derived parser return-type hints.
   The next concrete parser target is therefore the remaining
   qualified/member-template placeholder/materializer exits that still stamp
   only compatibility metadata after owner resolution or deferred lookup shape
   is already known. Also
   clean up the remaining legacy parser sites that still
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
   The focused regression for the newly-covered class-scope constexpr path is
   `test_template_static_constexpr_qualified_nested_owner_collision_ret0.cpp`.
   The focused regression for the newly-covered current-owner explicit-member-
   template placeholder path is
   `test_template_current_member_explicit_template_decltype_ret0.cpp`.
   The focused regression for the newly-covered substitution/materialization
   path is
   `test_template_qualified_explicit_function_id_definition_bound_ret0.cpp`.
   The focused regression for the newly-covered postfix qualified direct-call
   path is
   `test_template_namespace_qualified_explicit_function_id_definition_bound_ret0.cpp`.
   The focused regressions for the newly-covered non-receiver concrete-call
   builders are `test_template_direct_operator_definition_bound_ret0.cpp` and
   `test_template_postfix_address_call_definition_bound_ret0.cpp`.
   The focused guards for the newly-covered concrete receiver-call builders are
   `test_template_postfix_call_operator_default_arg_ret0.cpp` and
   `test_template_implicit_this_member_call_default_arg_ret0.cpp`.
   The focused guard for the newly-covered template-parameter function-pointer
   materialization path is
   `test_template_function_pointer_nttp_definition_bound_ret0.cpp`.
   The focused guards for the newly-covered explicit-qualified postfix
   member/operator builders are
   `test_template_explicit_base_member_call_default_arg_ret0.cpp`,
   `test_template_explicit_base_operator_call_default_arg_ret0.cpp`,
   `test_template_explicit_base_owner_template_member_template_call_default_arg_ret0.cpp`,
   and
   `test_template_explicit_base_owner_template_operator_template_call_default_arg_ret0.cpp`.
   The focused guard for variable-template replay preserving the full nested
   owner chain into the final direct-call target is
   `test_var_template_replay_dependent_member_template_call_ret0.cpp`.
   The member-function-pointer fast-path surface is now covered, including the
   `.*` / `->*` postfix path, MSVC ABI mangling for member-function-pointer
   qualifiers, and `_Is_memfunptr`-style owner-class deduction in partial
   specializations. The alias/materialization cluster that had still been
   visible in the full suite is now fixed as well: direct non-deferred alias
   rebinding once again wins over the lossy alias-materialization path for
   simple aliases, so pointer/cv/ref surface modifiers survive through
   `identity_t<T>`, `T*`, and member-alias forms. Focused guards:
   `test_alias_template_pointer_modifiers_ret0.cpp`,
   `test_alias_const_ptr_ret42.cpp`,
   `test_alias_ptrptr_ret42.cpp`,
   `test_alias_template_comprehensive_ret70.cpp`,
   `test_alias_two_pointers_ret30.cpp`, and
   `test_member_template_alias_ret250.cpp`. `pwsh ./tests/run_all_tests.ps1`
   now passes again for the full regular suite. As cleanup on top of that
   functional fix, the branch now routes definition-preferred qualified-owner
   member lookup through `MemberFunctionLookupShared.h`, removes the remaining
   bespoke qualified-id template-argument materializer in favor of
   `TemplateArgumentMaterialization.h`, and keeps that helper header out of the
   vcxproj lists. The next concrete target in
   this audit therefore moves back to standards-conformance expansion outside
   the passing core suite: the remaining expected-failure coverage
   (`test_cstddef.cpp`, `test_cstdio_puts.cpp`, `test_cstdlib.cpp`) and any
   follow-on canonical member-object-pointer carrier gap if a new ABI-sensitive
   regression reaches it.
   Long-term direction: stop teaching individual parser call sites to recover
   owner identity from short qualified spellings. Instead, preserve a shared
   structured qualified-owner record on the AST and route both parser
   materialization and sema fallback through one owner/type-owner-vs-namespace
   resolver so this pattern does not repeat in more qualified-call subpaths.

2. Only then spend complexity on current-instantiation /
   unknown-specialization expansion, and only for concrete typed-lookup or
   replay failures that remain after step 1.
