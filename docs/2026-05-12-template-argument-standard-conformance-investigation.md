# Template Argument Standard-Conformance Investigation

**Date:** 2026-05-12  
**Last updated:** 2026-06-08

This document tracks the standards-facing endpoint for template argument and
dependent-name work. It should describe the intended model and the shortest path
from the current implementation to that model, not a full branch history.

## Goal

Move FlashCpp toward a sema-owned template system where:

- dependency classification is explicit
- template arguments are classified against the actual target parameter
- dependent names preserve semantic identity end-to-end
- substitution, deduction, ranking, and materialization stay separate
- replay-heavy paths become invariant-driven instead of repair-driven

## What is already true enough to build on

- semantic lookup records cover the main template lookup paths
- covered non-dependent template-body lookup preserves definition-context
  binding
- several replay paths already preserve source identity and enough context to
  avoid AST-only intent recovery
- owner/member-template chains are preserved semantically in many covered alias
  and replay cases
- explicit member-template call handling now carries enough early argument
  information to avoid wrong cached overload reuse
- semantic direct-call resolution for normalized template/member calls now uses
  sema-owned overload-resolution argument typing instead of parser-owned
  argument collection, so dependent out-of-line member overloads are selected
  from the concrete substituted call signature seen during semantic analysis
- semantic `operator[]` resolution now also uses sema-owned
  overload-resolution argument typing plus receiver-const candidate filtering,
  so normalized subscripts preserve lvalue/rvalue index evidence and do not
  bind non-const members through const receivers
- semantic receiverless callable `operator()` resolution for dependent/local
  callable objects now also uses sema-owned overload-resolution argument typing
  plus receiver-const candidate filtering, so template-instantiated functors no
  longer rely on reduced expression typing for const or lvalue/rvalue overload
  selection
- sema now also rejects parser-selected non-const `operator()` targets on
  const dependent/local callable receivers once semantic lookup has shown there
  is no viable const-compatible overload, so this standards-visible negative
  case no longer compiles through member-call fallback
- dependent alias resolution is semantic-only: the textual recovery path in
  `resolveDependentMemberAlias(...)` has been removed in favor of preserved
  owner/member-chain records and instantiation-context bindings
- dependent nested member-template static constexpr chains now reuse typed
  owner/member-chain records, active template bindings, and inherited
  member-template lookup instead of hard-coded constexpr name scans
- out-of-line member replay attachment now fails instantiation when replay
  identity plus substituted-signature evidence cannot attach the definition,
  instead of silently continuing into later template instantiation
- plain out-of-line member replay now also requires positive signature
  evidence for single-candidate same-name attachment; unresolved
  substituted-signature outcomes no longer attach by default
- nested and partial-specialization constructor-template replay now fails
  directly when replay identity plus substituted-signature evidence cannot
  attach the out-of-line definition
- replay signature matching now reports explicit
  `Match`/`Mismatch`/`InsufficientEvidence` outcomes; attachment paths treat
  only `Match` as valid evidence
- replay attachment loops that previously flattened matching to boolean now
  preserve `InsufficientEvidence` and fail instantiation directly for
  nested/partial member-template and constructor-stub routes
- plain out-of-line member replay now also preserves `InsufficientEvidence`
  and fails instantiation directly instead of degrading to generic
  replay-attachment misses
- constructor replay sync into StructTypeInfo now requires
  `typeSpecifiersMatchForSignatureValidation(...)` evidence and no longer
  treats token/name shape equivalence as valid evidence
- `materializeAliasTemplateInstance` now delegates to
  `materializeInstantiatedMemberAliasTarget` for direct-parameter alias cases
  where the instantiated name resolves to a member type
- member type aliases preserve surface modifiers (pointer/reference/cv/array/
  function) across alias chains rather than collapsing to the terminal type
- function-template trailing return `decltype(...)` reparsing now runs against
  the instantiated parameter declarations rather than the pre-materialized
  template pattern
- non-explicit template-parameter materialization now preserves full
  pointer-to-member metadata for bound type arguments

## Highest-value remaining standards gap

The textual dependent-alias recovery path has been deleted; the former blockers
now resolve through preserved semantic records:

- `test_member_template_alias_preserves_outer_metadata_ret0.cpp`
- `test_alias_template_nested_member_value_ret42.cpp`
- `test_template_current_instantiation_alias_qualified_deeper_member_ret0.cpp`

With textual recovery gone, the next conformance step is keeping replay
attachment invariant-driven: tighten the remaining replay paths so valid cases
succeed via source identity plus canonical substituted-signature evidence rather
than name/arity repair scans, and expand current-instantiation /
unknown-specialization modeling only where it unblocks those paths.

## Priority order

1. ~~Preserve semantic owner/member-chain records in the remaining dependent
   alias callers.~~ **Done** — the textual recovery path is removed.

2. Continue tightening replay attachment so valid cases succeed via source
   identity plus canonical substituted-signature evidence, not shape-based
   repair scans.
   The next slice is likely the remaining unresolved-signature acceptance paths
   where replay can still succeed without enough evidence outside the now-
   tightened plain-member and member-template paths.

3. Unify the remaining semantic call-resolution entry points around the same
   sema-owned overload-resolution argument collector now used for normalized
   template/member direct calls. The 2026-06-04 follow-ups closed the concrete
   `operator[]` and dependent/local callable `operator()` gaps by sharing
   receiver-aware candidate filtering plus sema-owned argument typing with
   direct member-call resolution, and the 2026-06-08 follow-up closes the
   documented const-receiver no-viable callable diagnostic hole. The next step
   here is auditing any residual semantic call sites still outside that model,
   then removing the temporary parser-target compatibility fallbacks that
   remain once typed sema evidence is available.

4. Expand current-instantiation, dependent-base, and unknown-specialization
   handling only where that unblocks steps 2 and 3.

5. Leave broader cleanup for later unless it becomes a blocker:
   sema-owned ranking/deduction expansion, remaining repair-path removal, and
   sema-level modeling for aggregate-forwarding constructor sequences.

## Standards rules for follow-up work

- do not normalize textual reconstruction as acceptable semantics
- prefer invariant failures or proper diagnostics over silent repair in
  normalized flows
- keep compatibility behavior explicit, narrow, and temporary
- treat codegen-side recovery as debt to remove, not a design tool; when a
  late retry is still needed, it should delegate to typed lookup such as
  `resolveBaseClassMemberTypeChain`

## Next steps

1. Apply the sema-owned overload-resolution argument collector and the shared
   receiver-aware candidate partitioning to the remaining semantic
   call-resolution sites that still rely on parser-owned expression typing or
   reduced argument modeling beyond the now-fixed `operator[]` and
   dependent/local callable `operator()` routes.

2. Continue deleting replay-attachment acceptance paths that still allow
   insufficient substituted-signature evidence outside the now-hardened member
   and constructor routes.

3. Audit the remaining semantic call compatibility fallbacks that still reuse
   parser-selected targets after typed sema evidence is available, and remove
   them incrementally as each owning semantic path becomes complete.

4. Only after those are stable, extend current-instantiation and
   unknown-specialization modeling for the specific unresolved cases that remain.

## 2026-06-04 dependent decltype conformance note

The remaining pointer-to-member `decltype` failures were not primarily member
access bugs. The deeper standards gap was that free-function template trailing
return clauses were being reparsed before the instantiated parameter list
existed, and non-explicit parameter materialization could erase bound
pointer-to-member structure by reducing a template argument like `int Box::*`
to `int`.

The fix now:

- reparses saved free-function trailing return clauses after parameter
  materialization, with the concrete instantiated parameter declarations in
  scope
- copies the saved trailing-return/template parse positions onto the
  instantiated function before replay
- materializes non-explicit bound parameter types through the full substituted
  type-specifier path so member-pointer category/owner data survive deduction
- preserves member-pointer owner metadata when converting stored bound template
  arguments back into `TypeSpecifierNode`s

Standards-visible result:

- `decltype(wrapped.get())` in a function-template trailing return now
  preserves `T&`
- `decltype(wrapped.get().*pmd)` likewise preserves the lvalue reference and no
  longer admits invalid `Result*` forms

Validated with:

- `test_dependent_decltype_member_call_ref_ret0.cpp`
- `test_dependent_decltype_member_pointer_local_ret0.cpp`
- `test_dependent_decltype_member_pointer_local_fail.cpp`
- `test_dependent_decltype_arrow_member_pointer_ret0.cpp`
- `test_dependent_decltype_arrow_member_pointer_fail.cpp`
- `test_template_trailing_return_decltype_nttp_ret0.cpp`
- `test_template_trailing_return_namespace_lookup_ret0.cpp`
- full `pwsh tests/run_all_tests.ps1` on 2026-06-04, with only the unrelated
  pre-existing lambda-link failures remaining

## 2026-06-04 semantic operator[] conformance note

Auditing the remaining sema call-resolution entry points found a live C++20
conformance bug in semantic `operator[]` resolution: the overload set was
still classified from reduced expression typing and fallback selection ignored
receiver constness. That made two ordinary language rules unstable in normalized
subscript calls:

- a const receiver must not bind a non-const `operator[]`
- lvalue and prvalue index expressions must preserve the overload-resolution
  categories seen by ordinary member-call lookup

`SemanticAnalysis::tryResolveSubscriptOperator` now reuses the same
receiver-aware candidate partitioning and sema-owned overload-resolution
argument typing already used by normalized member-call resolution. This keeps
the semantic subscript path aligned with the broader sema-owned call model
instead of depending on reduced parser-shaped typing.

Validated with:

- `test_operator_subscript_sema_receiver_and_arg_overload_ret0.cpp`
- `test_operator_subscript_const_ambiguity_fail.cpp`
- `test_operator_subscript_const_ret42.cpp`
- `test_operator_subscript_overloads_ret42.cpp`
- `test_constexpr_operator_bracket_const_nonconst_ret0.cpp`
- full `pwsh tests/run_all_tests.ps1` on 2026-06-04

## 2026-06-04 callable/operator() conformance note

The remaining standards-callable regressions in normalized/deferred paths were:

- nested generic-lambda callable captures could remain zero-sized in closure
  layout, causing overlap with subsequent captures
- normalized direct-call lowering could hard-fail when sema-owned target
  metadata was missing, even for late/deferred call paths that still had typed
  lookup evidence
- unresolved callable-reference recursive self calls could miss the correct
  mangled target because self-argument qualifier handling diverged from ordinary
  recursive self lowering

The fix now:

- backfills unresolved by-value capture size/alignment from capture member type
  info and recalculates closure layout before emission
- preserves strict sema-owned direct-call target use when available, and limits
  compatibility behavior to an explicit metadata-missing boundary
- unifies unresolved callable-reference recursive self-call lowering with the
  ordinary recursive self path, including lvalue-reference self-argument
  qualifier treatment

Conformance debt to remove next:

- delete the metadata-missing direct-call compatibility boundary once sema
  always provides owned direct-call targets for these normalized/deferred calls

Validated with:

- `test_generic_lambda_callable_nested_clone_ret0.cpp`
- `test_generic_lambda_recursive_self_ret0.cpp`
- `test_lambda_cpp20_comprehensive_ret135.cpp`
- full `pwsh tests/run_all_tests.ps1` on 2026-06-04

## 2026-06-04 dependent callable operator() conformance note

The next live callable conformance gap was not another parser issue. It was the
late semantic resolution path for receiverless dependent/local callable objects:
`SemanticAnalysis::tryResolveCallableOperatorImpl` still classified overloads
from reduced expression typing and did not partition candidates by receiver
constness before fallback selection. That meant a template-instantiated functor
call could preserve the wrong overload category even after the concrete
callable type was known.

The fix now:

- reuses sema-owned overload-resolution argument typing for dependent/local
  callable `operator()` calls
- applies the same receiver-const candidate filtering used by ordinary member
  calls and semantic `operator[]`
- preserves recursive lambda self forwarding while treating sema-analyzed
  absent struct-callable targets as invalid in the covered codegen path

Validated with:

- `test_template_callable_operator_sema_receiver_and_arg_overload_ret0.cpp`
- `test_operator_call_sema_receiver_and_arg_overload_ret0.cpp`
- `test_callable_sema_resolved_ret0.cpp`
- `test_template_out_of_line_operator_call_ret0.cpp`
- `test_generic_lambda_recursive_self_ret0.cpp`
- full `pwsh tests/run_all_tests.ps1` on 2026-06-04

## 2026-06-08 dependent callable const-receiver conformance note

The documented remaining callable negative case is now closed: a dependent
`const Callable& c; c(...)` with only a non-const `operator()` used to compile
because the parser-selected member-call target could survive even after sema's
const-aware lookup found no viable callable overload.

The fix now:

- blocks reuse of that stale parser-selected non-const `operator()` target for
  const receivers once sema has no const-compatible callable candidate
- emits the failure from sema as a hard callable diagnostic instead of
  continuing through member-call fallback
- adds a stable template-instantiation `_fail` regression for the covered case

Validated with:

- `test_template_callable_operator_const_receiver_fail.cpp`
- `test_template_callable_operator_const_receiver_explicit_member_fail.cpp`
- `test_template_callable_operator_sema_receiver_and_arg_overload_ret0.cpp`
- `test_operator_call_sema_receiver_and_arg_overload_ret0.cpp`
- `test_callable_operator_default_arg_ret0.cpp`
- full `pwsh tests/run_all_tests.ps1` on 2026-06-08

## 2026-06-04 non-standard template test cleanup

A focused cleanup pass converted template-focused non-standard tests from
`docs/TEST_RUNNER_FAILURE_REPORT.md` to valid C++20 forms while preserving test
intent (friend declarations for class templates, protected-access setup, pack
expansion form, forward declarations, overload ambiguity trigger, and constexpr
initializer requirements).

Result: all updated tests pass `clang++ --target=x86_64-unknown-linux-gnu
-std=c++20 -pedantic-errors -fsyntax-only` and also pass targeted
`pwsh tests/run_all_tests.ps1` runs. No new missing-feature entry was required
for this subset.

## 2026-06-04 wider non-standard C++20 cleanup (non-extension set)

A wider pass converted the remaining non-extension non-standard tests to
portable C++20 forms and verified them with `clang++ -std=c++20
-pedantic-errors -fsyntax-only`. (Per repo preference, test edits avoid
`#include <cstddef>`.)

Now-passing tests from this wider pass were removed from
`docs/TEST_RUNNER_FAILURE_REPORT.md`.

Remaining blockers in FlashCpp for the edited non-extension set are now grouped
as implementation gaps rather than language-invalid test input:

- Standard library header/template ingestion gaps (`<compare>`, `<utility>`,
  `<typeinfo>`) affecting spaceship, forwarding, and RTTI test families.
- `constexpr` member-call binding in
  `test_identifier_binding_constexpr_function_call_member_access_prefers_static_member_function_ret42.cpp`.
- Structured-binding tuple customization lookup in
  `test_structured_binding_member_get_preferred_over_free_get_ret42.cpp`.
- `no_unique_address` and several `offsetof`/layout tests under the current
  header-ingestion/runtime-layout path.

## 2026-06-02 constexpr lambda conformance note

The updated constexpr-lambda tests exposed runtime capture-lowering bugs rather
than compile-time evaluation failures. Generated code now follows the C++20
closure model for captured `this` and nested captures:

- `[this]` member calls use the captured enclosing object pointer
- `[*this]` member calls use the copied object stored in the closure
- nested `[this]` captures propagate the effective enclosing object pointer,
  including when the enclosing lambda captured `*this`
- nested reference captures of enclosing closure members take the member's
  address directly and preserve the referenced object's real size
- constexpr member evaluation now preserves receiver cv-qualification through
  synthetic `this` bindings and rejects stale lowered non-const targets when
  evaluating calls from a const member body

Validated with all `tests/*constexpr_lambda*.cpp` tests on 2026-06-02.

## Validation guidance

For work in this area:

- add a focused regression first when a new gap is identified
- rerun `test_operator_subscript_sema_receiver_and_arg_overload_ret0.cpp` and
  `test_operator_subscript_const_ambiguity_fail.cpp` and
  `test_constexpr_operator_bracket_const_nonconst_ret0.cpp` when touching
  semantic subscript resolution or receiver-sensitive normalized-call lookup
- rerun the three former dependent-alias blocker tests when touching alias
  ownership or current-instantiation handling (they now exercise the
  semantic-only route)
- rerun the dependent member-template static constexpr regressions when touching
  nested owner/member-chain replay or static member initializer materialization
- run `pwsh tests/run_all_tests.ps1` before considering the slice complete
