# Semantic Analysis Status

**Last Updated:** 2026-06-02

This document is intentionally forward-looking. It should capture the current
ownership model, the invariants future work can rely on, and the next cleanup
targets. Detailed branch-by-branch history belongs in Git.

## Current pipeline

FlashCpp follows a parse -> sema -> IR pipeline:

- parser and sema are both created before parse (`src/FlashCppMain.cpp`)
- parser builds syntax, symbols, and template-instantiation scaffolding
- `SemanticAnalysis::run()` normalizes post-parse semantics
- `AstToIr` is expected to lower from sema-owned facts, not reconstruct them

## Ownership model

### Parser owns

- syntax construction and AST shape
- symbol creation and parse-time lookup surfaces
- template-instantiation mechanics and replay/materialization triggers

### Sema owns

- post-parse normalization
- expression/type/query state used by parser and codegen
- normalized-body invariants
- semantic call/type/conversion decisions that codegen consumes

### Codegen may still contain temporary compatibility behavior

- only in unresolved, dependent, non-normalized, or still-transitional paths
- never as the desired end state for normalized bodies

## Assumptions future work can rely on

- query-state APIs (`NotYetAnalyzed`, `AnalyzedAbsent`, `Available`) are the
  expected boundary for semantic facts
- the audited codegen paths no longer use direct
  `parser_.get_expression_type(...)` recovery
- normalized bodies already hard-fail in several places where sema-owned facts
  are required, including constructor annotation, call-result typing,
  `typeid(expr)` classification, range-expression typing, and several
  direct/member-call lowering paths
- direct-call compatibility-reason bookkeeping has been removed; sema now
  either provides a direct-call target or leaves the call unresolved for the
  existing non-normalized compatibility boundary
- unresolved ordinary direct-call terminals are still tracked explicitly in
  sema stats (`direct_call_unresolved_after_lookup`,
  `direct_call_unresolved_after_overload`) to preserve burn-down visibility
- post-overload member recovery has been removed from direct-call target
  resolution; unresolved post-overload terminals are now tracked directly via
  `direct_call_unresolved_after_overload`
- qualified direct calls now build member-function overload candidates from the
  resolved owner type (including current-instantiation/template-argument owner
  bindings) before lookup-empty recovery is considered
- lookup-empty member recovery has been removed from direct-call target
  resolution; empty lookup terminals are now tracked directly via
  `direct_call_unresolved_after_lookup`
- receiver-member recovery has been removed from direct-call target resolution;
  receiver calls now require a concrete parser/sema-resolved member target
- normalized template/member direct-call resolution now collects overload
  argument types from sema-owned expression typing via
  `tryCollectOverloadResolutionArgTypes(...)`, instead of reusing parser-owned
  argument typing that can retain stale dependent-template targets
- parser/template work now preserves substantially more owner/member-template
  identity for dependent aliases and replay-heavy paths before deduction-time
  resolution
- dependent alias resolution is now semantic-only: the textual `base::member`
  path in `resolveDependentMemberAlias(...)` has been removed in favor of
  preserved owner/member-chain records and instantiation-context bindings
- dependent member-template static constexpr chains now resolve through typed
  owner/member-chain records, active template bindings, inherited
  member-template lookup, and replayed static-initializer substitution instead
  of hard-coded constexpr name scans
- out-of-line member replay attachment now fails the instantiation when replay
  identity plus substituted-signature evidence cannot attach the definition,
  instead of logging and continuing into later template instantiation paths
- plain out-of-line member replay no longer accepts a single same-name source
  member on unresolved substituted-signature evidence alone; concrete
  same-signature matches now produce positive evidence and mismatches are
  rejected before replay attachment
- nested and partial-specialization out-of-line constructor-template replay
  misses now fail instantiation directly instead of logging and continuing to
  later lazy-constructor failures
- replay signature matching now returns explicit
  `Match`/`Mismatch`/`InsufficientEvidence` results, and replay attachment
  accepts only explicit `Match` evidence
- replay attachment sites that previously collapsed to boolean mismatch now
  preserve `InsufficientEvidence` and fail instantiation directly for
  nested/partial member-template and constructor-stub replay paths
- nested member-template replay attachment now also fails explicitly on
  ambiguous positive-evidence matches instead of accepting the first candidate
- plain out-of-line member replay attachment now also preserves
  `InsufficientEvidence` and fails instantiation directly instead of falling
  through to generic attachment misses
- StructTypeInfo constructor-template sync now requires positive
  `typeSpecifiersMatchForSignatureValidation(...)` evidence and no longer
  accepts token/name shape-only fallback equivalence
- constexpr member evaluation now preserves receiver cv-qualification through
  synthetic `this` bindings and rejects stale lowered non-const targets when
  evaluating calls from const member bodies
- runtime lambda lowering now treats captured `this` according to the C++20
  closure model: `[this]` calls use the captured object pointer, `[*this]`
  calls use the copied closure member, and nested captures propagate the
  effective enclosing object instead of the closure object

## Main remaining gaps

### 1. Remove remaining non-normalized direct-call recovery paths

The direct-call reason table is gone. The remaining work is to remove the
last non-normalized member-recovery paths so direct calls are sema-owned across
the whole pipeline and unresolved calls fail with clear diagnostics/invariants.

Near-term blocker in this area:

- dependent-unqualified calls still keep a provisional parser-callee fallback
  after POI lookup misses; removing it currently regresses
  `test_dependent_identifier_template_call_ret0.cpp`,
  `test_pack_expansion_in_template_body_ret0.cpp`, and
  `test_template_builtin_addressof_substitution_ret0.cpp`

Recommended next step:

- continue the Phase 1 burn-down in
  `docs/2026-04-04-codegen-name-lookup-investigation.md`

### 2. Unify remaining member/call lookup helpers

Several paths now apply the same standards rule independently: const receivers
only see const-qualified non-static members, while non-const receivers prefer
non-const overloads and then const overloads when no non-const match exists.
This is true for receiver-based constexpr member calls, current-struct
constexpr calls, and normalized template/member direct-call resolution, but the
candidate collectors are still structurally separate.

Recommended next step:

- extract a shared const-aware member-candidate collector for constexpr
  receiver-based lookup and current-struct lookup
- apply the sema-owned overload-resolution argument collector to the remaining
  semantic call-resolution entry points that still rely on parser-owned
  expression typing or reduced argument modeling

### 3. Keep template replay attachment evidence-driven

The dependent-alias cleanup is complete: semantic owner/member-chain records
now carry the former blocker cases, the textual `base::member` path is gone,
and dependent member-template static constexpr lookup now uses typed owner and
inherited-member lookup instead of hard-coded scans.

The next template task is narrower and more architectural:

- keep declaration replay attached by source identity plus canonical
  substituted-signature evidence
- continue shrinking the remaining unresolved-signature replay acceptance
  where name/shape still wins without enough evidence
- expand current-instantiation and unknown-specialization handling only where
  it unblocks those replay paths

### 4. Keep normalized-body invariants getting stricter

Any normalized-body path that still succeeds via compatibility behavior should
be converted to one of:

- sema-owned resolution
- `InternalError` for missing compiler-owned facts
- user-facing compile diagnostics for ill-formed code

## Standards endpoint

The C++20 target for this area is:

- parser records exact syntax and semantic identity inputs
- sema resolves final call/type/conversion meaning
- codegen emits only from sema-owned results
- normalized-body misses do not silently recover

## Related documents

- [docs/2026-04-04-codegen-name-lookup-investigation.md](/C:/Projects/FlashCpp/docs/2026-04-04-codegen-name-lookup-investigation.md)
- [docs/2026-05-12-template-argument-architecture-audit.md](/C:/Projects/FlashCpp/docs/2026-05-12-template-argument-architecture-audit.md)
- [docs/2026-05-12-template-argument-standard-conformance-investigation.md](/C:/Projects/FlashCpp/docs/2026-05-12-template-argument-standard-conformance-investigation.md)
- [docs/2026-04-27-fallback-comments-audit.md](/C:/Projects/FlashCpp/docs/2026-04-27-fallback-comments-audit.md)

## Legacy pointer docs

These are retained as pointers, not active planning centers:

- `docs/IMPLICIT_CAST_SEMA_PLAN.md`
- `docs/2026-03-12_IMPLICIT_CAST_SEMA_PLAN.md`
- `docs/2026-03-21_PARSER_TEMPLATE_SEMA_BOUNDARY_PLAN.md`
- `docs/2026-04-06-template-constexpr-sema-audit-plan.md`
- `docs/2026-05-16-sema-first-class-plan.md`
