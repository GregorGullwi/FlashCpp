# Front-end migration progress

Living state snapshot for
`docs/2026-08-24-front-end-rearchitecture-plan.md`. Each landed migration
pull request overwrites this file in place; this is not a history. Earlier
states are recoverable from git history.

Last updated: 2026-08-24 by branch
`codex/boundary0-asan-standard-header`

## Position

- Architecture boundary in progress: 0 (diagnosability and measurement)
- Last completed slice: pull request boundary 0 ASAN crash-diagnosability item
  — sanitizer-instrumented builds now leave fatal-signal ownership with ASAN
  instead of masking its reports with FlashCpp's custom crash handler; normal
  builds retain the existing platform crash handlers. A mutation-validated
  ownership probe lives in `tests/crash_handler/`, and the restored Ubuntu
  ASAN workflow runs runner self-tests, the ownership probe, and the full suite
  on pushes to `pre-flight` only

## Criteria completion

- Explicit exit criteria total: 78 (boundaries 0 through 11)
- Completed: 1/78 (1%)
  - Boundary 0 "diagnostics emitted outside the engine have a baseline and a
    named removal target in architecture boundary 11"
- Advanced, not completed:
  - Boundary 0 "every known architectural defect has a mutation-validated
    regression or a tracked expected failure": the ASAN crash-handler
    ownership defect now has a mutation-validated regression; the remaining
    architectural regression corpus and expected-failure manifest are still
    outstanding
  - Boundary 0 "choke-point counters and the remaining static inventories are
    visible in CI on a fixed corpus": the outside-engine counter is enforced
    in Windows CI on a fixed corpus; outstanding are the replay, AST-to-IR
    lookup, codegen-to-parser, post-parse typing, and template-routing
    counters plus the `'$'` static inventory (pull request boundary 4), and
    wiring this check into the Ubuntu lane once a Linux-generated baseline is
    verified there

## Effort estimate

- Implementation effort completed overall: 2-3%, confidence medium

## Remaining work

Replaces the previous remaining-work section entirely on every update.

Next blocker:

- None blocking.

Then, in order:

1. Pull request boundary 2: runner mechanics — diagnostic assertions over the
   `[Name#number]` contract, multi-TU and PIE modes, return-range validation,
   named expected-failure manifest with stale-entry detection.
2. Pull request boundary 3: first architectural regression slices
   (promotion, namespace-template identity, ambiguous member lookup),
   mutation-validated.
3. Pull request boundary 4: template facade plus the remaining choke-point
   counters and the `'$'` inline-parsing static inventory.

Named follow-ups carried forward:

- Wire `tests/run_migration_counters.ps1` into `ci-ubuntu.yml` after
  generating and verifying the baseline on a Linux build; corpus entries were
  chosen for platform-stable compile behavior but counts are only recorded for
  Windows so far.
- Pre-ICE raw `std::cerr` context dumps at `src/IrGenerator_MemberAccess.cpp`
  (struct-info-not-found and member-not-found paths) emit error text outside
  both the engine and the counter before throwing `InternalError`; decide
  their diagnostic ownership when ICE reporting moves behind
  `DiagnosticEngine`.
- Unify the ParseResult-channel pointer-to-reference twin at
  `src/Parser_Decl_DeclaratorCore.cpp:477` onto `DiagnosticId::
  PointerToReferenceType` once ParseResult carries structured diagnostics;
  converting it today would turn recoverable declarator probing into throws.
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
