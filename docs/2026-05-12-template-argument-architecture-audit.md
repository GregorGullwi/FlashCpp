# Template Argument Architecture Audit

**Date:** 2026-05-12  
**Last updated:** 2026-06-09

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
reference qualification or pointer-to-member owner identity. The most recent
callable follow-up now also makes the dependent `const Callable& c; c(...)`
negative case sema-owned, so parser-selected non-const `operator()` targets no
longer compile through const receivers after sema has already found no viable
const-compatible callable target. The next ordinary-member follow-up now closes
the same const-receiver hole for dependent `const T& obj; obj.member(...)`
calls, so stale parser-selected non-const member targets no longer compile once
shared sema lookup has found no const-compatible overload; function-pointer
members are explicitly excluded by requiring real member-function ownership
before raising the diagnostic. The next receiver-member follow-up now also
blocks parser-selected fallback after typed sema has already proven a dependent
member call ambiguous, so replay/materialization remains the biggest remaining
gap while semantic-call fallback debt narrows toward the non-receiver/direct-call
cases that still have temporary compatibility behavior.

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
- sema now also rejects parser-selected non-const `operator()` targets on
  const dependent/local callable receivers once semantic lookup has shown there
  is no viable const-compatible overload, so this negative path no longer
  compiles through fallback attachment
- sema now also rejects parser-selected non-const ordinary member-function
  targets on const dependent/member-call receivers once shared const-aware
  lookup has shown there is no const-compatible overload, while leaving
  function-pointer member calls on the indirect-call path
- sema now also rejects parser-selected ordinary member-call fallback once
  shared typed overload resolution has already proven the dependent receiver
  call ambiguous
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
   `operator[]` and dependent/local callable `operator()` resolution. The
   2026-06-08/2026-06-09 follow-ups close the documented const-receiver
   no-viable callable and ordinary-member diagnostic holes; the next useful
   expansion is to audit any other semantic call-resolution sites that still do
   not share that collector or the receiver-aware candidate partitioning around
   it, and then remove the temporary compatibility fallbacks that remain once
   typed sema evidence is present.

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

1. Audit the remaining non-receiver / direct-call compatibility branches that
   still allow ordinary calls to continue after typed sema evidence has
   produced no viable target or an ambiguity, now that the receiver-sensitive
   negative cases are closed for `operator[]`, callable `operator()`, and
   ordinary member functions.

2. Tighten the remaining replay-attachment sites that can still succeed without
   positive substituted-signature evidence outside the now-hardened plain-member,
   member-template, and constructor-template routes.

3. Audit the remaining semantic call compatibility fallbacks that still reuse
   parser-selected targets after typed sema evidence is available, and remove
   each fallback once the owning semantic path is fully covered.

4. Expand current-instantiation / unknown-specialization modeling only for the
   concrete cases that still block standards-conforming typed lookup after steps
   1-3.

## 2026-06-09 ordinary member const-receiver sema note

Continuing the semantic-call fallback audit found that the shared
`resolveCallArgAnnotationTarget(...)` / `tryAnnotateCallArgConversionsImpl(...)`
path still left one standards-visible ordinary member-call hole open: a
dependent `const T& obj; obj.member(...)` could compile when the parser had
already attached a non-const member target, even though sema's const-aware
member lookup found no viable const-compatible overload.

This slice now:

- blocks reuse of that stale parser-selected non-const member target once
  shared const-aware lookup has proven the receiver has no const-compatible
  overload for the named member
- raises the failure from the shared sema call-annotation path as a hard member
  call diagnostic instead of letting normalized/template calls continue into
  codegen fallback
- keeps function-pointer member calls valid by requiring actual member-function
  ownership before applying the const-receiver diagnostic guard

Validated with:

- `test_template_member_call_const_receiver_fail.cpp`
- `test_template_callable_operator_const_receiver_fail.cpp`
- `test_funcptr_nested_template_struct_ret0.cpp`
- `test_typedef_function_ptr_ret0.cpp`
- full `pwsh tests/run_all_tests.ps1` on 2026-06-09

## 2026-06-09 ordinary member ambiguity sema note

Continuing the same fallback audit found a second receiver-member hole: a
dependent member call that became ambiguous at instantiation could still
compile because sema reduced the typed overload result to "no unique target"
and then fell back to the parser-selected member candidate.

This slice now:

- preserves ambiguity information across the shared receiver-member typed
  overload helper instead of collapsing it to a generic miss
- blocks parser-selected fallback once shared sema has already proven the
  receiver-member call ambiguous
- raises the covered ambiguity from the shared call-annotation path as a hard
  member-call diagnostic

Validated with:

- `test_template_member_call_ambiguous_fail.cpp`
- `test_template_member_call_no_viable_overload_fail.cpp`
- `test_template_member_call_const_receiver_fail.cpp`
- `test_funcptr_nested_template_struct_ret0.cpp`
- `test_typedef_function_ptr_ret0.cpp`
- full `pwsh tests/run_all_tests.ps1` on 2026-06-09

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

## 2026-06-08 dependent callable const-receiver diagnostic note

The next callable audit slice closed the remaining documented compile-through
case for dependent/local callable objects: a templated `const Callable& c;
c(...)` could still compile when the parser had already attached a non-const
`operator()` target, even though sema's const-aware member lookup found no
viable const-compatible overload.

This slice now:

- blocks the stale parser-selected member-call target from being reused once
  semantic lookup has proven that a const receiver has no const-compatible
  `operator()` candidate to bind
- raises the failure from sema as an explicit callable diagnostic rather than
  allowing the member-call fallback to continue
- locks the case down with a stable template-instantiation `_fail` regression

Validated with:

- `test_template_callable_operator_const_receiver_fail.cpp`
- `test_template_callable_operator_const_receiver_explicit_member_fail.cpp`
- `test_template_callable_operator_sema_receiver_and_arg_overload_ret0.cpp`
- `test_operator_call_sema_receiver_and_arg_overload_ret0.cpp`
- `test_callable_operator_default_arg_ret0.cpp`
- full `pwsh tests/run_all_tests.ps1` on 2026-06-08

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
