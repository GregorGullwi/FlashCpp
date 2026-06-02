# Template Argument Architecture Audit

**Date:** 2026-05-12  
**Last updated:** 2026-06-02

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
targets. The biggest remaining gap is keeping every replay/materialization path
equally evidence-driven rather than shape-driven.

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
   call-argument typing inside sema with normalized semantic argument typing.
   The next useful expansion is to audit the remaining semantic call-resolution
   sites that still do not share that collector.

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
   share `tryCollectOverloadResolutionArgTypes(...)`, especially operator-call
   and other normalized-call paths where parser-owned expression typing can
   diverge from sema-owned substituted types.

2. Tighten the remaining replay-attachment sites that can still succeed without
   positive substituted-signature evidence outside the now-hardened plain-member,
   member-template, and constructor-template routes.

3. Expand current-instantiation / unknown-specialization modeling only for the
   concrete cases that still block standards-conforming typed lookup after steps
   1 and 2.

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
- the three former textual-path blockers listed above when touching dependent
  alias ownership (they now exercise the semantic-only route)
- the dependent member-template static constexpr regressions when touching
  nested qualified-id replay, inherited member-template lookup, or static member
  initializer emission
- `pwsh tests/run_all_tests.ps1` before closing the slice
