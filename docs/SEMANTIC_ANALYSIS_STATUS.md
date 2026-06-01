# Semantic Analysis Status

**Last Updated:** 2026-06-01

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
- direct-call compatibility bookkeeping is now explicit in sema rather than
  hidden inside codegen retries
- direct-call compatibility reason tracking has been narrowed further by
  removing `StructMemberLookupMiss`; unresolved ordinary direct-call terminals
  now fold into `QualifiedOrOrdinaryNameLookupMiss`
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

## Main remaining gaps

### 1. Remove the last direct-call compatibility reasons

The direct-call area is much narrower now, but sema still carries explicit
compatibility reasons for a small set of legacy or non-normalized flows. The
goal is to remove those one by one until normalized direct calls either resolve
semantically or fail with a proper diagnostic/invariant.

Recommended next step:

- continue the Phase 1 burn-down in
  `docs/2026-04-04-codegen-name-lookup-investigation.md`

### 2. Keep template replay attachment evidence-driven

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

### 3. Keep normalized-body invariants getting stricter

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
