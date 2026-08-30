# Front-end migration progress

Living state snapshot for
`docs/2026-08-24-front-end-rearchitecture-plan.md`. Each landed migration
pull request overwrites this file in place; this is not a history. Earlier
states are recoverable from git history.

Last updated: 2026-08-30 after pull request boundary 5

## Position

- Architecture boundary in progress: 1 (front-end context, arenas, identities,
  and entities). Pull request boundaries 1 through 5 are complete; architecture
  boundary 1 exit criteria remain open through pull request boundary 6 and
  follow-on boundary-1 work.
- Pull request boundary 5 delivered `FrontendContext` (active-context stack),
  strong ID types (`ScopeId`, `DeclId`, `EntityId`, `ExprId`, `TypeId`,
  `TemplateDeclId`), four allocation-domain placeholders, `MonotonicScratchArena`
  with probe registry rollback/commit and discarded-byte accounting, `--perf-stats`
  arena telemetry (scratch domain, string-table stats, InlineVector spill count),
  `MigrationTelemetryConfig.h` compile-time gates (default on), and doctest
  coverage for IDs, scratch transactions, and nested registry checkpoints.
- Global forwarding into `FrontendContext` is telemetry-only so far (string-
  table entry count and spelling bytes); syntax/semantic/IR domain bytes and
  record counts remain unwired.

## Pull request boundary status (1–5)

All complete.

| Boundary | Delivered |
|----------|-----------|
| 1 | Diagnostic engine, crash handler, initial conversions |
| 2A–2F | Filename diagnostic contract, legacy `_fail` conversion |
| 3 | Architectural regression probes (tracked expected failures) |
| 4 | `TemplateEngine` facade, migration choke-point counters |
| 5 | `FrontendContext`, strong IDs, scratch arena, arena telemetry |

Pull request boundaries are not the same as architecture boundaries 0–11.
Architecture boundary 0 tracking slices are substantially closed; architecture
boundary 1 is started, not finished.

## Criteria completion

- Explicit exit criteria total: 78 (boundaries 0 through 11)
- Completed: 4/78 (5%)
  - Boundary 0 "diagnostics emitted outside the engine have a baseline and a
    named removal target in architecture boundary 11"
  - Boundary 0 "structured diagnostics can be asserted by tests"
  - Boundary 0 "choke-point counters and the remaining static inventories are
    visible in CI on a fixed corpus"
  - Boundary 1 "IDs cannot be constructed from pointers"
- Advanced, not completed:
  - Boundary 0 "every known architectural defect has a mutation-validated
    regression or a tracked expected failure"
  - Boundary 1 "every template instantiation entry point passes through the
    facade": external callers use `TemplateEngine`; parser-internal paths
    remain direct until boundary 6/8A
  - Boundary 1 "a scratch transaction can allocate declarations and types, fail,
    and leave every committed registry unchanged": proven in doctest; not wired
    to parser tentative parsing
  - Boundary 1 "discarded scratch bytes are measured and bounded": measured;
    no implementation limit yet
  - Boundary 1 "arena bytes, record counts, string-table bytes, and selected
    InlineVector spill counts are reported through FrontendContext": partial
    (scratch, string-table, InlineVector spills under `--perf-stats`)
  - Boundary 1 remaining exit criteria (persistent scopes, declaration merging,
    `ChunkedAnyVector` compile-time guard, full template-facade coverage,
    leaving scope does not destroy lookup information): pull request boundary 6
    onward

## Effort estimate

- Implementation effort completed overall: 10-12%, confidence medium

## Remaining work

Replaces the previous remaining-work section entirely on every update.

Next blocker:

- Scope lookup still depends on destructive parser scope exit in the legacy
  `SymbolTable` stack (`ScopeHandle` levels). Persistent scopes with stable
  `ScopeId`, parent links, and cursor-style exit in `FrontendContext` are
  required before the first spelling-based namespace recovery path can be
  deleted (pull request boundary 6).

Then, in order:

1. Continue architecture boundary 1: wire syntax/semantic/IR domain byte
   accounting, per-record counts, and broader global forwarding into
   `FrontendContext`.

Named follow-ups carried forward:

- Before architecture boundary 10A, approve a parser-family routing table for
  the single translation-unit parse entry point.
- Wire `tests/run_migration_counters.ps1` into `ci-ubuntu.yml` after
  generating and verifying the baseline on a Linux build.
- Pre-ICE raw `std::cerr` context dumps at `src/IrGenerator_MemberAccess.cpp`
  emit error text outside both the engine and the counter before throwing
  `InternalError`; decide ownership when ICE reporting moves behind
  `DiagnosticEngine`.
- Declaration-parse errors masked by the top-level expression-statement
  fallback: any masked rejection site must route through the shared
  declaration dispatch or its test is deleted before a structured ID is
  assigned (see `docs/KNOWN_ISSUES.md`).
- Telemetry compile-time gates in `MigrationTelemetryConfig.h` default on; set
  individual flags to 0 for shipping builds when ready.
- Blanket `noexcept` on member functions stays deferred until boundaries 5-8
  shrink the exception surface to invariant-only paths.

## Active findings

Current findings only; delete entries when their resolution lands.

- The unity doctest target's MSBuild ClangCL configuration crashes the clang
  frontend against the VS18 STL headers (LLVM 20.1 vs STL 14.51 mismatch).
  Unit tests are validated through the direct LLVM clang-cl driver instead.
  Resolution home: toolchain alignment of `tests/FlashCppTest`.
- Pre-existing unity-suite failure
  `SemanticAnalysis:*QueryTracksAnalysisState` reproduces on clean `main`;
  details and suspected shared-static cause live in docs/KNOWN_ISSUES.md.
  Owner: sema query lifecycle.
- Declaration-parse errors can be masked by the top-level expression-statement
  fallback; details and the conversion rule for affected sites live in
  docs/KNOWN_ISSUES.md. Owner: parser declaration dispatch.
