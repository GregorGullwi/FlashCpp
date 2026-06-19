# Template Argument Standard-Conformance Investigation

**Date:** 2026-05-12  
**Last updated:** 2026-06-18

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
- that structured retry is now reused by the current-member static fast path
  too, so definition-bound replay coverage no longer depends on a separate
  weaker argument-typing branch there
- string-literal user-defined literal calls now preserve
  `FunctionCallDefinitionLookupRecord` using the synthesized literal-operator
  name and argument list instead of carrying only `mangled_name`
- untyped ordinary direct-call fallback exits no longer need a parser-selected
  callee to keep parse-time typing alive: they now carry an explicit parser
  return-type hint on the call node plus either a deferred
  definition-context lookup record or a deferred dependent-unqualified lookup
  record, so later semantic resolution can re-run the correct lookup instead of
  leaning on mangled-name or parser-target compatibility
- `resolveDeferredQualifiedTemplateCall(...)` no longer performs ordinary
  static-member overload recovery for qualified type owners; non-template
  qualified direct calls now stay on sema-owned owner/overload resolution
  instead of inheriting a parser-side compatibility branch
- constexpr evaluation now reuses sema-owned qualified direct-call resolution
  before falling back to parser-owned deferred template instantiation, and that
  shared resolver accepts an explicit current-type fallback so class-scope
  `static constexpr` initializers participate in the same nested-owner/type-
  owner-vs-namespace split as member-function sema
- instantiation-time resolved-call materialization in `ExpressionSubstitutor`
  now rebuilds `FunctionCallDefinitionLookupRecord` for qualified/member-
  template direct calls once substitution has concrete arguments plus a
  concrete `FunctionDeclarationNode`, instead of preserving only copied
  `qualified_name`/`mangled_name` compatibility metadata in those paths
- postfix qualified direct-call and concrete member-postfix fast paths in
  `Parser_Expr_PostfixCalls.cpp` now attach structured call metadata whenever
  they already hold a concrete `FunctionDeclarationNode`, instead of stopping
  at copied `qualified_name`/`mangled_name` compatibility metadata in those
  parser materializers
- direct operator function-id calls in `Parser_Expr_PrimaryExpr.cpp` and the
  postfix declaration-address fast path in `Parser_Expr_PostfixCalls.cpp` now
  preserve structured non-receiver call metadata too, so late sema no longer
  has to rediscover their definition-bound target from a parser-selected
  callee alone
- the main concrete receiver-call builders now preserve shared metadata too:
  postfix `operator()` finalization plus the implicit-`this`, member-operator,
  and callable-object `operator()` builders in `Parser_Expr_PrimaryExpr.cpp`
  all attach structured receiver-call metadata once they already know the
  exact `FunctionDeclarationNode`
- template-parameter function-pointer direct-call materialization in
  `Parser_Expr_PrimaryExpr.cpp` now preserves
  `FunctionCallDefinitionLookupRecord` when definition-context lookup already
  resolves the bound function, so late sema does not have to rebind that call
  from compatibility data after later same-name overloads appear
- explicit-qualified postfix member/operator call builders in
  `Parser_Expr_PostfixCalls.cpp` now resolve concrete owner-qualified targets
  for forms such as `this->Base::touch()` and `this->Base::operator()()` when
  the receiver type and arguments are concrete, instead of materializing a
  placeholder `auto` member call that later loses semantic identity
- the shared qualified-owner member-call helper in
  `Parser_Expr_QualLookup.cpp` now treats concrete `Type::member(...)` owner
  calls more semantically too: non-template fallback selection is limited to
  static members, uses real overload resolution once argument types are
  concrete, and unresolved template-instantiation owners preserve a structured
  dependent-qualified owner record even when `StructTypeInfo` is already
  present instead of dropping back to `qualified_name` plus a parser return-type
  hint alone
- that qualified static-owner path now also stays conservative at the right
  semantic boundary: if a concrete owner already has visible static-member
  candidates but parser-time overload ranking still cannot prove the final
  target, the call now defers with canonical owner identity plus structured
  qualified-call metadata instead of either reviving a parser-selected fallback
  or hard-rejecting a call that later semantic resolution can still complete
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
- parser-side call-argument pack expansion now preserves unmatched complex
  `expr...` nodes until substitution instead of treating "no function-parameter
  pack matched" as a successful expansion; that closes the standards-visible
  leak where `typeCode<Rest>()...` in `add3(..., tail)` lost pack semantics
  before sema could scalarize the bound template pack
- deferred qualified/member-template calls now preserve explicit template
  arguments, parser return-type hints, and dependent-qualified lookup records
  as separate metadata so later semantic resolution can replay the correct
  lookup instead of inheriting a parser-selected callee as final meaning
- deferred namespace-qualified explicit-template calls now preserve exact
  definition-bound replay metadata only when the visible function-template
  candidate is unique and no exact specialization is registered for that same
  qualified template name, so specialization-first lookup remains available
  during later instantiation
- the unqualified/current-owner explicit-member-template placeholder path in
  `Parser_Expr_PrimaryExpr.cpp` now preserves the same split for deferred
  class-scope member-template calls: it keeps a structured current-
  instantiation `DependentQualifiedNameRecord`, and parser return-type hints
  now come from the matched current-owner member-template declaration instead
  of whichever unrelated same-name global template happened to be found first
- sema now distinguishes real type owners from namespace qualifiers before it
  consumes definition-bound compatibility metadata for qualified direct calls,
  which fixes current-instantiation nested-owner cases like
  `Runner<T>::Ops::read(value)` being stolen by an unrelated global
  `Ops<T>::read`
- explicit qualified member-template calls now preserve the full nested owner
  identity in the parser/materialization layer when the owner spelling names a
  real nested owner, so `Ops::template read<int>(value)` inside `Runner<T>`
  no longer rebinds through an unrelated global `Ops<T>::read`
- that owner recovery now also walks enclosing nested class/member-function
  contexts from inner to outer, so a nested class body can still resolve an
  owner declared on the enclosing current instantiation through the same
  explicit qualified member-template path
- that owner rewrite is now constrained to true nested-owner extensions only
  (for example `Runner<int>::Ops` from `Ops`), preventing alias/self-owner
  qualified calls from being eagerly collapsed onto unrelated concrete owner
  names while still closing the standards-visible collision above
- instantiated template class members now preserve function-pointer member
  signatures through alias/template-argument recovery plus source
  `StructTypeInfo` fallback, closing the standards-visible gap where indirect
  calls through concrete function-pointer data members lost their return
  signature during materialization
- postfix function-pointer member-call placeholders now synthesize the real
  return type from the member signature instead of carrying a fake scalar
  return, so overload resolution on calls like `pick(this->callback())`
  follows the actual C++ type system
- `.*` / `->*` member-function-pointer postfix calls now preserve the pointed-
  to return shape and parser return-type hint through template-body
  materialization instead of degrading to fake scalar placeholders
- member-function-pointer template arguments now preserve declaring-class
  identity through substitution/materialization, MSVC mangling now covers
  member-function-pointer cv/ref/noexcept qualifiers plus ordinary
  pointer-to-member declarator forms, and `_Is_memfunptr`-style partial
  specializations now deduce the owner class instead of treating it as opaque
  text

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

- the biggest ordinary identifier-call coupling is now gone: parse-time
  return-type availability and final semantic call-target authority are
  separated on the call node / lookup-record boundary instead of being tied to
  a parser-selected callee
- the remaining compatibility boundary is now concentrated in other parser
  materialization paths that still carry only `mangled_name` or
  `qualified_name`, especially niche qualified/member-template materializers
  that do not yet route through the same deferred-lookup model; the main
  postfix qualified/member fast paths plus the direct operator/declaration-
  address non-receiver paths are now covered too, and the main concrete
  receiver-call builders plus template-parameter function-pointer call
  materialization are covered as well, and the explicit-qualified postfix
  member/operator placeholder builders are now covered too, and ordinary
  function-pointer member postfix placeholders plus the `.*` / `->*`
  member-function-pointer fast paths now preserve the real return shape, and
  the shared qualified static-owner helper no longer relies on a same-arity
  shape match for its non-template fallback or drops structured owner metadata
  just because the owner `StructTypeInfo` is already present, so the remaining
  work is no longer that whole pointer-to-member surface; it is
  the smaller set of still-uncovered placeholder/materializer exits plus the
  canonical `TypeCategory::MemberObjectPointer` carrier gap where the
  underlying member type is not yet preserved strongly enough for every
  ABI-sensitive consumer
- the just-fixed qualified-owner collision confirms the right ownership split:
  non-template qualified calls must first decide whether the left-hand side is
  a type owner or a namespace qualifier, and only then may compatibility
  records participate; the old parser helper branch that did ordinary
  static-member recovery inside `resolveDeferredQualifiedTemplateCall(...)` is
  now gone, so the remaining work in this sub-area is preserving structured
  semantic metadata in the qualified/member-template materializers that still
  stop at compatibility-only records after that owner split
- the explicit qualified member-template nested-owner collision is now closed
  at the parser/materialization source rather than by adding a later semantic
  compatibility branch; the old ordinary static-member recovery branch in
  `resolveDeferredQualifiedTemplateCall(...)` is now gone too, and the main
  postfix qualified/member parser path now preserves structured call metadata
  as well, so the remaining work in this sub-area is the narrower set of
  parser/materialization sites that still stop at compatibility metadata even
  after the owner resolution has succeeded
- the newly-covered current-owner explicit-member-template path confirms that
  short class-scope spellings must follow the same rule too: deferred
  `pick<int>(...)`-style calls inside a current instantiation cannot source
  parser-time return-type visibility from an unrelated
  `lookupAllTemplates(...)` result once the matching member-template owner is
  already known
- the newly-covered generic namespace-qualified explicit-template path confirms
  the same rule for specialization-aware lookup: preserving a unique parse-time
  primary template declaration is only conforming when no exact specialization
  is registered for that qualified template name. Otherwise deferred
  instantiation must remain free to select the explicit specialization first,
  as in `ns::sum<Args...>()` with `template<> sum<int>()`
- the postfix explicit-qualified member/operator path now follows the same rule
  for concrete owner-qualified member-template ids: `this->Base::template pick<int>()`
  and `this->Base::template operator()<int>()` now parse and instantiate through
  the base-qualified owner instead of failing before overload selection
- the unresolved explicit-base member-template placeholder in that postfix path
  is now structural as well: `this->Base::template pick<T>()` preserves the
  recovered base owner as deferred qualified metadata, keeps the matched
  member-template return type on the placeholder instead of a raw `auto` stub,
  and substitution instantiates the concrete base-qualified member template
  once `T` becomes concrete instead of rebinding by unqualified member name
- the sibling postfix explicit-qualified operator-template placeholder is now
  structural too: short-owner `holder.Base::template operator()<int>()`
  spellings canonicalize the nested owner before instantiation, preserve the
  deferred qualified-owner record when parsing must remain deferred, and keep
  the matched operator-template return type instead of letting a global
  same-spelled owner satisfy the parse
- the newly fixed explicit-template-argument pack-expansion leak confirms the
  right layer for this class of problem: preserve the parser-only pack node
  until substitution instead of teaching deeper sema/template-argument logic to
  guess when a plain call expression was really meant to be expanded
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

- identify the remaining concrete direct-call builders outside the now-covered
  postfix qualified/member helpers, then replace each typed case with
  preserved structured target metadata before touching the sema fallback;
  after the now-covered typed qualified-member, string-literal UDL, postfix
  qualified/member, direct operator/declaration-address, and substitution-
  materialization paths plus template-parameter function-pointer call
  preservation, the `.*` / `->*` member-function-pointer pass, and
  explicit-qualified postfix member/operator preservation, the next targets
  are the remaining qualified/member-template compatibility-only materializers
  plus any smaller placeholder builders that still return immediately without
  a structured lookup record. The shared qualified static-owner helper is no
  longer one of those weak spots: its non-template fallback now routes through
  real static-member overload selection, and unresolved
  template-instantiation owners keep structured owner metadata even when the
  owner `StructTypeInfo` is already available

2. Expand current-instantiation and unknown-specialization handling only where
   it unblocks concrete replay or typed-lookup failures still remaining after
   step 1.

## Next steps

1. Continue eliminating compatibility-only parser metadata in the remaining
   qualified/member-template materializers that still stop at
   `qualified_name` / `mangled_name` after owner resolution already succeeded.
   Immediate next target: audit the remaining qualified/member-template
   placeholder/materializer exits in `Parser_Expr_PrimaryExpr.cpp` and
   `Parser_Expr_PostfixCalls.cpp` that still stop at compatibility metadata
   even after owner resolution or deferred lookup shape is already known. The
   unqualified/current-owner explicit-member-template placeholder path is now
   covered; it preserves structured current-instantiation owner metadata and
   member-template-derived parser return-type hints instead of deferring with
   only `qualified_name`. The generic namespace-qualified explicit-template
   placeholder path is now covered too, including the specialization-aware
   rule that suppresses exact primary-template binding when a registered exact
   specialization must stay visible; the concrete postfix explicit-qualified
   member/operator template-id path without owner template arguments is now
   covered too, and the postfix owner-template-id variant is covered as well:
   `parse_member_postfix()` now preserves `Base<T>`-style owner template-ids
   through the `::template` continuation for both member-template and
   operator-template calls instead of dropping that structure before deferred
   lookup. The short-owner postfix explicit-qualified operator-template
   placeholder is now covered too: nested `Base::template operator()<int>()`
   spellings canonicalize the owner before instantiation and preserve the same
   deferred qualified-owner metadata plus parser return-type hints when
   parsing must stay deferred. The next uncovered parser target is therefore
   narrower again: the remaining qualified/member-template
   placeholder/materializer exits that still stamp only compatibility metadata
   after owner resolution or deferred lookup shape is already known, followed
   by the remaining whole-call sema synchronization hook.
2. If another pointer-to-member issue appears, close the remaining canonical
   `TypeCategory::MemberObjectPointer` carrier gap by preserving the
   underlying member type explicitly instead of relying on declarator-shaped
   `member_class + pointer_depth` forms in ABI-sensitive paths.
3. Improve parser diagnostics in the remaining copy-initialization path by
   preserving nested expression or qualified-call failures instead of
   collapsing them to the generic "Failed to parse initializer expression".
   This is not the next conformance blocker, but the latest concrete-owner
   qualified-call slice exposed it clearly enough that it should stay on the
   list.
4. Keep replay/identity and broader
   current-instantiation/unknown-specialization work in reserve for concrete
   failures; the remaining conformance pressure is now centered on the last
   parser compatibility boundaries rather than on broad replay drift.

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
- `test_template_qualified_static_member_same_arity_decltype_ret0.cpp`
- `test_member_template_func_in_specialization_ret0.cpp`
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
- `test_template_explicit_base_operator_template_decltype_ret0.cpp`
- `test_template_explicit_base_operator_template_nested_owner_collision_ret0.cpp`
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
- full `pwsh tests/run_all_tests.ps1` before considering the slice complete

## Next steps

1. Continue eliminating compatibility-only parser metadata in the remaining
   resolved direct-call sites, now focusing on the qualified/member-template
   materializers that still have not adopted the deferred lookup + parser
   return-type hint split.
   Immediate follow-up: move those paths onto the same model now used by
   untyped ordinary calls, and keep
   `test_template_static_constexpr_dependent_hidden_friend_ret0.cpp` in the
   guard set so parser-time constexpr users never again need a parser-selected
   callee for return-type visibility. The old ordinary-static-member
   compatibility branch in `resolveDeferredQualifiedTemplateCall(...)` is now
   removed, and constexpr qualified direct calls reuse the same sema-owned
   type-owner / namespace-qualifier split with explicit current-type fallback
   for class-scope evaluation. The substitution-time qualified/member-template
   materializer now also rebuilds `FunctionCallDefinitionLookupRecord` once the
   substituted call is concrete, and the shared
   `try_parse_member_template_function_call(...)` helper now uses real static-
   member overload resolution plus structured deferred owner metadata instead of
   a same-arity fallback. The current-owner explicit-member-template
   placeholder path in `Parser_Expr_PrimaryExpr.cpp` is now on that model too:
   it preserves a structured current-instantiation owner record and sources its
   parser return-type hint from the matched member-template declaration instead
   of an unrelated global template. The next concrete step is therefore to
   finish moving the remaining parser materializers onto preserved
   `FunctionCallDefinitionLookupRecord` or explicit deferred lookup records,
   focusing on the other qualified/member-template placeholder/materializer
   exits that still stop at compatibility metadata after owner resolution
   already succeeded, and then remove the whole-call sema synchronization hook
   by pushing that work back to earlier semantic ownership. This slice closes
   the explicit-base postfix member-template placeholder and the sibling
   postfix explicit-qualified operator-template placeholder: short nested-owner
   `Base::template operator()<int>()` spellings no longer instantiate against
   a global same-spelled owner during trailing `decltype(...)` parsing, and
   deferred cases preserve the same structured owner metadata plus parser
   return-type hints as the member-template branch.
   Immediate focused follow-up under that item:
   the nested-owner explicit member-template instantiation bug is now fixed and
   guarded by
   `test_template_qualified_member_template_nested_owner_collision_ret0.cpp`
   plus the multi-segment owner-chain regression
   `test_template_qualified_member_template_nested_owner_chain_collision_ret0.cpp`
   and the enclosing-scope nested-class regression
   `test_template_qualified_member_template_enclosing_owner_collision_ret0.cpp`.
   The new focused guard for class-scope constexpr nested-owner recovery is
   `test_template_static_constexpr_qualified_nested_owner_collision_ret0.cpp`.
   The new focused guard for current-owner explicit-member-template deferred
   target preservation is
   `test_template_current_member_explicit_template_decltype_ret0.cpp`.
   The new focused guard for substitution-time qualified explicit-template
   target preservation is
   `test_template_qualified_explicit_function_id_definition_bound_ret0.cpp`.
   The new focused guard for postfix qualified direct-call target preservation
   is
   `test_template_namespace_qualified_explicit_function_id_definition_bound_ret0.cpp`.
   The new focused guards for non-receiver concrete-call target preservation
   are `test_template_direct_operator_definition_bound_ret0.cpp` and
   `test_template_postfix_address_call_definition_bound_ret0.cpp`.
   The new focused guards for concrete receiver-call metadata preservation are
   `test_template_postfix_call_operator_default_arg_ret0.cpp` and
   `test_template_implicit_this_member_call_default_arg_ret0.cpp`.
   The new focused guard for template-parameter function-pointer target
   preservation is
   `test_template_function_pointer_nttp_definition_bound_ret0.cpp`.
   The new focused guards for explicit-qualified postfix member/operator target
   preservation are
   `test_template_explicit_base_member_call_default_arg_ret0.cpp`,
   `test_template_explicit_base_operator_call_default_arg_ret0.cpp`,
   `test_template_explicit_base_operator_template_decltype_ret0.cpp`,
   `test_template_explicit_base_operator_template_nested_owner_collision_ret0.cpp`,
   `test_template_explicit_base_owner_template_member_template_call_default_arg_ret0.cpp`,
   and
   `test_template_explicit_base_owner_template_operator_template_call_default_arg_ret0.cpp`.
   The focused guard for the concrete-owner "candidate-bearing but not yet
   parser-resolved" rule is
   `test_member_template_func_in_specialization_ret0.cpp`.
   The new focused guard for variable-template replay preserving the full
   nested owner chain into the eventual direct-call target is
   `test_var_template_replay_dependent_member_template_call_ret0.cpp`.
   The member-function-pointer fast-path surface is now covered, including the
   `.*` / `->*` postfix path, MSVC ABI mangling for member-function-pointer
   qualifiers, and `_Is_memfunptr`-style owner-class deduction in partial
   specializations. The remaining alias/materialization cluster from this slice
   is now fixed too: direct non-deferred alias rebinding is again preferred for
   simple aliases, which preserves concrete pointer/cv/ref surface modifiers
   through plain and member alias templates. Focused guards:
   `test_alias_template_pointer_modifiers_ret0.cpp`,
   `test_alias_const_ptr_ret42.cpp`,
   `test_alias_ptrptr_ret42.cpp`,
   `test_alias_template_comprehensive_ret70.cpp`,
   `test_alias_two_pointers_ret30.cpp`, and
   `test_member_template_alias_ret250.cpp`. The full regular suite now passes
   again via `pwsh ./tests/run_all_tests.ps1`. As cleanup on top of that
   functional fix, the branch now centralizes definition-preferred
   qualified-owner member lookup in `MemberFunctionLookupShared.h`, reuses
   `TemplateArgumentMaterialization.h` for the remaining qualified-id
   template-argument materialization path, and removes the helper header from
   the vcxproj lists. The next concrete target in this
   investigation therefore shifts to standards-conformance growth outside the
   green suite: the remaining expected-failure coverage
   (`test_cstddef.cpp`, `test_cstdio_puts.cpp`, `test_cstdlib.cpp`) and any
   future canonical member-object-pointer carrier gap if another ABI-sensitive
   failure exposes it.
   Long-term direction: replace these per-call-site parser repairs with one
   shared structured qualified-owner representation plus a single resolver used
   by both parser materialization and later sema fallback, so future
   qualified/member-template paths do not regress by rebuilding owner meaning
   from short spellings.
   In parallel with that audit, finish deleting the remaining legacy
   parser-side `expr...` loops that still duplicate pack-expansion behavior
   instead of routing through the shared helper and the new "preserve when not
   truly expandable" rule.

2. Use any concrete failures left after step 1 to drive the next
   current-instantiation / unknown-specialization expansion rather than
   widening that model preemptively.
