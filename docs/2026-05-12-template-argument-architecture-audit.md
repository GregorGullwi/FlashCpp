# Template Argument Architecture Audit

**Date:** 2026-05-12  
**Last updated:** 2026-06-04

This document is not a release log. It should explain what the current template
architecture can assume, what is still structurally wrong, and what the next
highest-value cleanup should be.

## Executive summary

The template system has moved away from broad AST-only replay and toward
owner-aware semantic records plus replay-first attachment. The legacy textual
`base::member` dependent-alias path has now been removed; dependent member
alias resolution is semantic-only. The newest high-impact fix also moves
normalized template-call overload lookup onto sema-owned argument typing, so
replayed dependent member overloads no longer inherit stale parser-selected
targets. A concrete follow-up now brings semantic `operator[]` resolution onto
that same receiver-aware, sema-owned argument-typing path, so const receivers
and lvalue/rvalue index overloads no longer depend on reduced parser-shaped
typing. The latest template-`decltype` fix now also reparses function-template
trailing return clauses against materialized parameter declarations and keeps
bound member-pointer metadata intact in the non-explicit parameter
materialization path, so dependent trailing-return `decltype` no longer drops
reference qualification or pointer-to-member owner identity. The biggest
remaining gaps are (1) keeping every replay/materialization path equally
evidence-driven rather than shape-driven and (2) finishing the last hard
diagnostic cleanup around no-viable dependent callable-object `operator()`
cases.

## What the current design can assume

- covered non-dependent template-body lookup preserves definition-context
  binding
- major out-of-line replay paths now prefer source-member identity plus
  substituted-signature evidence over name/arity scans
- constructor and member-template replay are increasingly owner-aware and
  evidence-driven rather than shape-driven
- dependent owner/member-template chains already have a shared semantic
  materialization route in the covered cases
- explicit member-template calls now carry call-argument information early
  enough to avoid caching obviously wrong overloads
- sema-owned direct-call resolution for normalized template/member calls now
  collects overload-resolution argument types from semantic expression typing
  instead of parser-owned call-argument typing, so dependent out-of-line member
  overloads resolve by the concrete substituted call signature seen during sema
- semantic `operator[]` resolution now shares sema-owned overload-resolution
  argument typing and the same receiver-const candidate partitioning used by
  direct member-call lookup, so normalized subscripts no longer bind non-const
  members through const receivers or lose lvalue/rvalue index evidence
- semantic receiverless callable `operator()` resolution for dependent/local
  callable objects now also shares sema-owned overload-resolution argument
  typing plus receiver-const candidate partitioning, so template-instantiated
  functors no longer depend on reduced expression typing for lvalue/rvalue or
  const overload selection
- nested member-template alias materialization now preserves substantially more
  outer owner/member-template metadata through parsing, rebinding, and
  materialization
- dependent alias resolution is now semantic-only: the legacy textual
  `base::member` path in `resolveDependentMemberAlias(...)` has been removed,
  and resolution flows through the preserved `DependentQualifiedNameRecord`,
  instantiation-context bindings, and `materializeInstantiatedMemberAliasTarget`
- `materializeAliasTemplateInstance` now delegates to
  `materializeInstantiatedMemberAliasTarget` for direct-parameter alias
  cases (`template<T> using id = T`) where `instantiated_name` is empty
  but the alias node is non-null
- member type aliases preserve surface modifiers (pointer/reference/cv/array/
  function) across alias chains via the shared
  `typeAliasPreservesSurfaceModifiers()` helper, so collapsing an alias to its
  terminal type no longer silently drops indirection
- dependent member-template static constexpr chains are resolved through typed
  owner/member-chain records, active template bindings, and inherited
  member-template lookup; the hard-coded constexpr scan for matching generated
  member-template names is gone
- out-of-line member replay no longer treats failed replay attachment as
  recoverable: if replay identity plus substituted-signature evidence cannot
  attach the definition, instantiation now fails instead of silently
  continuing into later template instantiation paths
- plain out-of-line member replay now also requires positive signature
  evidence even for single-candidate same-name attachment: concrete exact
  matches produce positive evidence, while unresolved `std::nullopt` outcomes
  no longer attach by default
- nested and partial-specialization out-of-line constructor-template replay
  now treats attachment misses as instantiation failures, matching the ordinary
  constructor and member-function replay paths
- replay signature matching now uses explicit result states
  (`Match`/`Mismatch`/`InsufficientEvidence`) so attachment sites consume
  positive evidence only
- replay attachment loops that previously reduced matching to boolean now
  preserve `InsufficientEvidence` and fail instantiation directly for
  nested/partial member-template and constructor-stub paths
- plain out-of-line member replay now also preserves `InsufficientEvidence`
  and fails instantiation directly instead of degrading to generic
  replay-attachment misses
- constructor replay sync into StructTypeInfo now requires signature-validation
  evidence and no longer accepts token/name shape equivalence for
  mismatched parameter types
- function-template trailing-return reparse now happens after parameter
  materialization when a saved `->` position exists, so dependent
  `decltype(param_expr)` sees the same concrete parameter declarations used by
  the instantiated function body
- non-explicit function-template parameter materialization now preserves full
  bound type metadata for pointer-to-member arguments instead of collapsing
  them to the pointee/base type

## Main remaining architectural gap

The residual textual `base::member` path in `resolveDependentMemberAlias(...)`
has been removed. The previously-blocking recordless cases now resolve through
semantic owner/member-chain records that are preserved end-to-end at
parse/materialization time:

- `test_member_template_alias_preserves_outer_metadata_ret0.cpp`
- `test_alias_template_nested_member_value_ret42.cpp`
- `test_template_current_instantiation_alias_qualified_deeper_member_ret0.cpp`

The remaining problem is no longer textual recovery. It is keeping replay
attachment evidence-driven across the remaining declaration/materialization
paths: a few routes still lean on name/arity shape or parser-carried bindings
instead of source identity plus canonical substituted-signature evidence.
Tightening those is the next best cleanup target.

## Recommended implementation order

1. ~~Remove the last recordless dependent-alias routes.~~ **Done.**
   Semantic owner/member-chain records are now preserved in the three former
   blocker cases and the textual `base::member` path has been deleted.

2. Keep replay attachment evidence-driven.
   Continue tightening declaration replay so attachment succeeds because source
   identity and canonical substituted signatures match, not because a
   shape-based repair scan guessed correctly. The next likely slice is the remaining
   unresolved-signature acceptance paths that still tolerate shape-first replay
   attachment outside the now-tightened plain-member/member-template paths.

3. Extend sema-owned lookup unification where it unlocks step 2.
   The recent member-call fix closed one concrete gap by replacing parser-owned
   call-argument typing inside sema with normalized semantic argument typing,
   and the 2026-06-04 follow-ups applied the same model to semantic
   `operator[]` and dependent/local callable `operator()` resolution. The next
   useful expansion is to finish the remaining hard diagnostic cleanup around
   no-viable dependent callable objects, then audit any other semantic
   call-resolution sites that still do not share that collector or the
   receiver-aware candidate partitioning around it.

4. Extend dependent-name modeling only where it unlocks steps 2 and 3.
   Richer current-instantiation and unknown-specialization handling still
   matters, but it should be pulled in to unblock concrete replay/alias gaps,
   not pursued as a detached refactor.

5. Leave lower-priority uplift for later unless it blocks the above.
   This includes broader sema-owned ranking/deduction cleanup and sema-level
   modeling for aggregate-forwarding constructor cases.

## Architectural rules for follow-up work

- prefer semantic records over textual reconstruction
- prefer replay-first source identity over instantiated-member scans
- do not add new broad repair paths for unresolved replay attachment
- route any remaining late materialization through typed lookup helpers rather
  than rebuilding `owner::member` strings by hand
- if a path cannot preserve enough metadata, document the gap explicitly and
  keep the compatibility surface narrow

## Next steps

1. Audit the remaining semantic call-resolution entry points that do not yet
   share `tryCollectOverloadResolutionArgTypes(...)` or the shared
   receiver-aware candidate partitioning beyond the now-fixed `operator[]` and
   dependent/local callable `operator()` routes.

2. Finish the hard diagnostic follow-up for dependent callable objects: a local
   scratch `const Callable& c; c(...)` negative case still found an older path
   that compiled after sema declined to resolve a viable `operator()`, so the
   next callable slice should make that failure sema-owned and add a stable
   `_fail` regression.

3. Tighten the remaining replay-attachment sites that can still succeed without
   positive substituted-signature evidence outside the now-hardened plain-member,
   member-template, and constructor-template routes.

4. Expand current-instantiation / unknown-specialization modeling only for the
   concrete cases that still block standards-conforming typed lookup after steps
   1-3.

## 2026-06-04 dependent decltype trailing-return note

Auditing the template-argument conformance backlog found a standards-visible gap
in function-template trailing return materialization: dependent
`decltype(...)` clauses could be reparsed before the instantiated parameter list
existed, and the non-explicit parameter-materialization path could flatten a
bound pointer-to-member template argument to its pointee type. Together those
bugs lost lvalue-reference qualification in cases like
`decltype(wrapped.get())`, lost member-pointer owner metadata in
`decltype(wrapped.get().*pmd)`, and allowed invalid pointer-to-reference forms
to compile.

The fix now:

- reparses saved trailing return clauses after free-function template parameter
  materialization and against the instantiated parameter declarations
- copies the saved trailing-return/template positions onto the instantiated
  function before that reparse
- rebuilds non-explicit bound parameter types through the full substituted
  type-specifier path instead of a TypeIndex-only substitution
- preserves pointer-to-member owner metadata when reconstructing a
  `TypeSpecifierNode` from bound template-argument records

Validated with:

- `test_dependent_decltype_member_call_ref_ret0.cpp`
- `test_dependent_decltype_member_pointer_local_ret0.cpp`
- `test_dependent_decltype_member_pointer_local_fail.cpp`
- `test_dependent_decltype_arrow_member_pointer_ret0.cpp`
- `test_dependent_decltype_arrow_member_pointer_fail.cpp`
- `test_template_trailing_return_decltype_nttp_ret0.cpp`
- `test_template_trailing_return_namespace_lookup_ret0.cpp`
- full `pwsh tests/run_all_tests.ps1` on 2026-06-04, with only the pre-existing
  lambda-linking failures remaining outside this area

## 2026-06-04 semantic operator[] note

Auditing the remaining semantic call-resolution entry points found one
standards-visible live bug in `SemanticAnalysis::tryResolveSubscriptOperator`:
subscript overload resolution still used reduced expression typing and did not
filter member candidates by receiver constness before fallback selection. That
allowed const objects to bind non-const `operator[]` members and could erase
lvalue/rvalue index evidence needed for overload selection.

The fix now:

- partitions `operator[]` candidates with the same receiver-const rule used by
  ordinary member-call lookup
- resolves the subscript index through sema-owned overload-resolution argument
  typing rather than raw `inferExpressionType(...)`
- shares the small overload-set helpers with ordinary member-call resolution so
  future receiver-sensitive fixes cannot drift between the two paths

Validated with:

- `test_operator_subscript_sema_receiver_and_arg_overload_ret0.cpp`
- `test_operator_subscript_const_ambiguity_fail.cpp`
- `test_operator_subscript_const_ret42.cpp`
- `test_operator_subscript_overloads_ret42.cpp`
- `test_constexpr_operator_bracket_const_nonconst_ret0.cpp`
- full `pwsh tests/run_all_tests.ps1` on 2026-06-04

## 2026-06-04 callable/operator() standards follow-up note

Continuing the standards-callable cleanup exposed three coupled regressions in
normalized/deferred lambda call paths:

- nested generic-lambda closures could place callable and scalar captures at the
  same offset when callable capture size metadata remained unresolved
- normalized direct-call lowering failed too early when sema-owned target
  metadata was absent, even though the call still had enough typed context for
  late/deferred lookup resolution
- unresolved callable-reference recursive self calls (`self(self, ...)`) could
  lower with mismatched self-argument qualifier expectations, breaking mangled
  target resolution

The fix now:

- backfills unresolved by-value capture size/alignment from capture-member type
  information and recalculates closure layout before emission
- keeps strict sema-owned direct-call target behavior when metadata exists, and
  constrains compatibility behavior to an explicit metadata-missing boundary
- aligns unresolved callable-reference recursive self lowering with the ordinary
  recursive self path (including lvalue-reference self argument handling)

Remaining debt:

- the metadata-missing direct-call compatibility boundary is still temporary and
  should be removed once sema always materializes owned direct-call targets for
  these normalized/deferred paths

Validated with:

- `test_generic_lambda_callable_nested_clone_ret0.cpp`
- `test_generic_lambda_recursive_self_ret0.cpp`
- `test_lambda_cpp20_comprehensive_ret135.cpp`
- full `pwsh tests/run_all_tests.ps1` on 2026-06-04

## 2026-06-04 dependent callable operator() sema note

Continuing the same call-resolution audit found that receiverless dependent
callable-object resolution still lagged behind direct member calls and
`operator[]`: `SemanticAnalysis::tryResolveCallableOperatorImpl` still used
reduced expression typing and did not partition candidates by receiver
constness before fallback selection. That left template-instantiated functor
calls vulnerable to stale lvalue/rvalue collapse and const/non-const drift even
though the concrete callable type was already known in sema.

This slice now:

- routes dependent/local callable `operator()` overload selection through
  sema-owned overload-resolution argument typing
- partitions callable candidates with the same receiver-const rule already used
  by direct member calls and semantic `operator[]`
- keeps the codegen-side callable hard-stop narrow enough to preserve recursive
  lambda self forwarding while still treating sema-analyzed absent struct
  callable targets as invalid in the covered path

Validated with:

- `test_template_callable_operator_sema_receiver_and_arg_overload_ret0.cpp`
- `test_operator_call_sema_receiver_and_arg_overload_ret0.cpp`
- `test_callable_sema_resolved_ret0.cpp`
- `test_template_out_of_line_operator_call_ret0.cpp`
- `test_generic_lambda_recursive_self_ret0.cpp`
- full `pwsh tests/run_all_tests.ps1` on 2026-06-04

Remaining debt:

- a local scratch negative case (`const Callable& c; c(...)` with only a
  non-const call operator) still found an older compile-through path, so the
  next callable follow-up should add a sema-owned hard diagnostic and land a
  stable `_fail` regression for that case

## 2026-06-02 constexpr lambda capture-lowering note

The updated constexpr-lambda tests exposed runtime-only closure-lowering gaps:
compile-time evaluation already accepted the C++20 cases, but generated code
mis-modeled captured `this` inside lambdas and nested lambdas. The current fix
keeps lambda member-call receivers standards-aligned by:

- treating `this->member()` inside `[this]` as a call on the captured enclosing
  object pointer, not on the closure object
- treating `this->member()` inside `[*this]` as a call on the closure's copied
  object member
- propagating nested `[this]` captures through the effective enclosing object
  pointer, including when the enclosing object is a `[*this]` copy
- taking the address of enclosing closure members for nested by-reference
  captures instead of assuming a local variable exists
- preserving the referenced object's real size when loading through a
  by-reference capture
- preserving receiver cv-qualification through constexpr synthetic `this`
  bindings, including unqualified member calls from const member bodies

Follow-up: the constexpr evaluator now applies the same const-aware selection
rule in the receiver-based and current-struct paths, but the implementations are
still structurally separate. The next cleanup should extract one shared
member-candidate collector so future overload fixes cannot drift between paths.

Validated with all `tests/*constexpr_lambda*.cpp` tests on 2026-06-02.

## Validation guidance

When changing this area, always rerun:

- the focused regression that motivated the slice
- `test_operator_subscript_sema_receiver_and_arg_overload_ret0.cpp` and
  `test_operator_subscript_const_ambiguity_fail.cpp` and
  `test_constexpr_operator_bracket_const_nonconst_ret0.cpp` when touching
  semantic subscript resolution or receiver-sensitive normalized-call lookup
- the three former textual-path blockers listed above when touching dependent
  alias ownership (they now exercise the semantic-only route)
- the dependent member-template static constexpr regressions when touching
  nested qualified-id replay, inherited member-template lookup, or static member
  initializer emission
- `pwsh tests/run_all_tests.ps1` before closing the slice
