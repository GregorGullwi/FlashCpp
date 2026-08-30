# Front-end migration progress

Living state snapshot for
`docs/2026-08-24-front-end-rearchitecture-plan.md`. Each landed migration
pull request overwrites this file in place; this is not a history. Earlier
states are recoverable from git history.

Last updated: 2026-08-30 by branch `codex/boundary-4-template-facade-counters`

## Position

- Architecture boundary in progress: 1 (front-end context, arenas, identities,
  and entities) after closing pull request boundary 4
- Pull request boundaries 1 through 4 are complete. Boundary 4 introduced the
  `TemplateEngine` facade shell, routed external template-instantiation entry
  points through it, instrumented token replay, post-parse parser typing,
  AST-to-IR semantic queries, codegen-to-parser callbacks, template old-engine
  routes, and dollar-identity recovery counters, and added the static
  `find('$')` inventory guard.
- The fixed migration corpus now baselines all seven runtime counters plus the
  dollar inventory (17 inline `find('$')` sites in `src/`).

## Criteria completion

- Explicit exit criteria total: 78 (boundaries 0 through 11)
- Completed: 3/78 (4%)
  - Boundary 0 "diagnostics emitted outside the engine have a baseline and a
    named removal target in architecture boundary 11"
  - Boundary 0 "structured diagnostics can be asserted by tests"
  - Boundary 0 "choke-point counters and the remaining static inventories are
    visible in CI on a fixed corpus"
- Advanced, not completed:
  - Boundary 0 "every known architectural defect has a mutation-validated
    regression or a tracked expected failure": converted diagnostic families
    and the boundary-3 promotion, namespace-template-identity, and ambiguous-
    member-lookup probes are mutation-validated; five boundary-3 cases remain
    tracked runtime expected failures, and the remaining architectural corpus
    is still outstanding
  - Boundary 1 "every template instantiation entry point passes through the
    facade": external subsystem callers now route through `TemplateEngine`;
    parser-internal instantiation paths remain direct until boundary 6/8A
  - Boundary 1 "arena bytes, record counts, string-table bytes, and selected
    InlineVector spill counts are reported through FrontendContext": not started
    (pull request boundary 5)

## Effort estimate

- Implementation effort completed overall: 7-9%, confidence medium

## Remaining work

Replaces the previous remaining-work section entirely on every update.

Next blocker:

- Pull request boundary 5: `FrontendContext`, strong ID types, scoped arena
  domains, scratch rollback tests, and arena telemetry forwarding.

Then, in order:

1. Pull request boundary 6: persistent scopes and first spelling-based recovery
   deletion.

Named follow-ups carried forward:

- Before architecture boundary 10A, approve a parser-family routing table for
  the single translation-unit parse entry point. Boundaries 10A through 10F
  now separate indexed token input, parser transactions, syntax-only
  declarations, syntax-only expressions and statements, bounded parser control
  flow, and deletion of the parser service locator; no family may be routable
  to both legacy and migrated parsers.
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
