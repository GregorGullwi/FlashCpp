# Front-end migration progress

Living state snapshot for
`docs/2026-08-24-front-end-rearchitecture-plan.md`. Each landed migration
pull request overwrites this file in place; this is not a history. Earlier
states are recoverable from git history.

Last updated: 2026-08-24 by PR #1898

## Position

- Architecture boundary in progress: 0 (diagnosability and measurement)
- Last landed slice: pull request boundary 1 item — invariant-failure
  downgrades removed from per-root IR conversion (PR #1898)

## Criteria completion

- Explicit exit criteria total: 78 (boundaries 0 through 11)
- Completed: 0/78 (0%)
- Advanced, not completed:
  - Boundary 0 "internal invariant failures cannot be reported as success"
    (remaining boundary-0 machinery outstanding: diagnostic engine,
    choke-point counters, ASAN crash investigation)

## Effort estimate

- Implementation effort completed overall: 1-2%, confidence medium

## Remaining work

Replaces the previous remaining-work section entirely on every update.

Next blocker:

- `DiagnosticEngine` core does not exist: stable diagnostic IDs, severity,
  `SourceLocation`/`SourceRange`, structured arguments, attached notes, and
  template-instantiation context. Converted diagnostics and `_fail.cpp`
  diagnostic assertions depend on it.

Then, in order:

1. Pull request boundary 1 remainder: diagnostic engine core, convert three
   to five diagnostics that already have correct source locations, record
   remaining raw diagnostic sites, begin the outside-engine counter.
2. Pull request boundary 2: runner mechanics (diagnostic assertions,
   multi-TU and PIE modes, return-range validation) and the named
   expected-failure manifest with stale-entry detection.
3. Pull request boundary 3: first architectural regression slices
   (promotion, namespace-template identity, ambiguous member lookup),
   mutation-validated.

## Active findings

Current findings only; delete entries when their resolution lands.

- The checked-in `x64/Sharded` binary can be stale relative to `main`;
  testing against it produces phantom failures (observed 2026-08-24:
  8 unrelated failures that do not reproduce after a rebuild). Resolution
  home: runner mechanics, pull request boundary 2 — rebuild or staleness
  guard before baselining.
