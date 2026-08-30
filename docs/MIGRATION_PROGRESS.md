# Front-end migration progress

Living state snapshot for
`docs/2026-08-24-front-end-rearchitecture-plan.md`. Each landed migration
pull request overwrites this file in place; this is not a history. Earlier
states are recoverable from git history.

Last updated: 2026-08-30 by branch `codex/boundary-3-regression-validation`

## Position

- Architecture boundary in progress: 0 (diagnosability and measurement)
- Pull request boundaries 1 through 3 are complete. The filename-encoded
  diagnostic contract is authoritative, the legacy `_fail.cpp` inventory and
  internal-failure compatibility are removed, and the legacy-entry count is
  zero.
- Boundary 3 has eight mutation-validated probes. The `auto`, constexpr,
  template-deduction, and two namespace-template identity probes remain tracked
  runtime expected failures; the `sizeof`, overload-ranking, and ambiguous-
  member-lookup probes pass.
- The full Windows-suite validation on 2026-08-30 covered 2,959 regular tests,
  253 encoded negative tests, and one multi-translation-unit case; all
  compile/link phases passed, with no crashes, runtime mismatches, or
  negative-contract failures. Five tracked positive expected failures matched.
  The latest Linux full-suite run covered 2,929 single-file tests, 281
  negative tests, one multi-translation-unit case, and five tracked positive
  expected failures; it had no crashes or mismatches.

## Criteria completion

- Explicit exit criteria total: 78 (boundaries 0 through 11)
- Completed: 2/78 (3%)
  - Boundary 0 "diagnostics emitted outside the engine have a baseline and a
    named removal target in architecture boundary 11"
  - Boundary 0 "structured diagnostics can be asserted by tests"
- Advanced, not completed:
  - Boundary 0 "every known architectural defect has a mutation-validated
    regression or a tracked expected failure": converted diagnostic families
    and the boundary-3 promotion, namespace-template-identity, and ambiguous-
    member-lookup probes are mutation-validated; five boundary-3 cases remain
    tracked runtime expected failures, and the remaining architectural corpus
    is still outstanding
  - Boundary 0 "choke-point counters and the remaining static inventories are
    visible in CI on a fixed corpus": the outside-engine counter is enforced
    on Windows CI over the fixed corpus including the encoded literal tests;
    outstanding are the replay, AST-to-IR lookup, codegen-to-parser,
    post-parse typing, and template-routing counters plus the `'$'` static
    inventory (pull request boundary 4), and wiring this check into the
    Ubuntu lane once a Linux-generated baseline is verified there

## Effort estimate

- Implementation effort completed overall: 5-7%, confidence medium

## Remaining work

Replaces the previous remaining-work section entirely on every update.

Next blocker:

- There is no local validation blocker for the boundary-3 snapshot. All eight
  architectural probes pass or match their declared runtime expected-failure
  stage, and every decisive assertion rejects an inverted mutation.

Then, in order:

1. Pull request boundary 4: template facade plus the remaining choke-point
   counters and the `'$'` inline-parsing static inventory.

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
