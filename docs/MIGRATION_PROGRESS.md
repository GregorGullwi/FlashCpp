# Front-end migration progress

Living state snapshot for
`docs/2026-08-24-front-end-rearchitecture-plan.md`. Each landed migration
pull request overwrites this file in place; this is not a history. Earlier
states are recoverable from git history.

Last updated: 2026-08-25 by branch
`plan-negative-diagnostic-boundaries`

## Position

- Architecture boundary in progress: 0 (diagnosability and measurement)
- Pull request boundary in progress: 2A. New negative tests encode
  the exact expected `DiagnosticId` number multiset in terminal filename
  segments such as `_e1001.cpp` and `_e1003_e1051.cpp`. Both runners ignore
  location, severity, diagnostic name, and note role, preserve repeated IDs,
  and require a clean source-rejection exit with no object. FlashCpp now uses
  distinct success, source-rejection, and internal-failure process statuses,
  so crashes, timeouts, driver failures, and missing worker results cannot
  satisfy a negative test or an expected positive failure. Internal failures
  remain strict except for the seven-name legacy compatibility described
  below. The 259 genuine root `_fail.cpp` names from `origin/main` are
  frozen behind an exact count and SHA-256 inventory; every entry has exactly
  one legacy or same-stem encoded representation, with the three structured
  declarator tests migrated first. `expected_failures.tsv` separately tracks
  broken positive tests by terminal stage and fails stale expectations.
- Seven frozen legacy tests currently terminate through internal/compiler
  failure paths. A second immutable inventory grants only their still-present
  original `_fail.cpp` representations a temporary status-2 compatibility
  when no object is produced. Encoded successors and every unlisted test remain
  strict. Both runners report 7 active against baseline 7, direction down, with
  deletion at boundary 2F. Exact tests and failure ownership are recorded in
  `docs/KNOWN_ISSUES.md`.

## Criteria completion

- Explicit exit criteria total: 78 (boundaries 0 through 11)
- Completed: 2/78 (3%)
  - Boundary 0 "diagnostics emitted outside the engine have a baseline and a
    named removal target in architecture boundary 11"
  - Boundary 0 "structured diagnostics can be asserted by tests"
- Advanced, not completed:
  - Boundary 0 "every known architectural defect has a mutation-validated
    regression or a tracked expected failure": the ASAN crash-handler
    ownership defect has a mutation-validated regression, the three
    declarator-family diagnostics are filename-pinned, and the named positive
    expected-failure manifest now rejects stale stages; the remaining
    architectural regression corpus is still outstanding
  - Boundary 0 "choke-point counters and the remaining static inventories are
    visible in CI on a fixed corpus": the outside-engine counter is enforced
    in Windows CI on a fixed corpus; outstanding are the replay, AST-to-IR
    lookup, codegen-to-parser, post-parse typing, and template-routing
    counters plus the `'$'` static inventory (pull request boundary 4), and
    wiring this check into the Ubuntu lane once a Linux-generated baseline is
    verified there

## Effort estimate

- Implementation effort completed overall: 4-6%, confidence medium

## Remaining work

Replaces the previous remaining-work section entirely on every update.

Next blocker:

- None blocking boundary 2A locally. PowerShell runner and Windows compiler
  validation remain CI-only in the current environment.

Then, in order:

1. Pull request boundary 2B: convert the frozen lexer, parser, declarator, and
   source-structure negative-test slice by shared diagnostic owner.
2. Pull request boundaries 2C through 2F: continue converting the frozen legacy negative
   tests in bounded diagnostic-owner batches. Each batch assigns stable IDs at
   shared compiler emission sites and renames only its exact inventory slice;
   boundary 2F deletes `_fail.cpp` classification, both frozen inventories, and
   the seven-test internal-failure compatibility.
3. Pull request boundary 3: first architectural regression slices
   (promotion, namespace-template identity, ambiguous member lookup),
   mutation-validated.
4. Pull request boundary 4: template facade plus the remaining choke-point
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

- Seven legacy negative tests use the immutable, status-2 compatibility
  inventory. The active count is 7 against baseline 7 and may only fall.
  Encoded successors lose the exception immediately. Details live in
  `docs/KNOWN_ISSUES.md`; deletion target is boundary 2F.
- The unity doctest target's MSBuild ClangCL configuration crashes the clang
  frontend against the VS18 STL headers (LLVM 20.1 vs STL 14.51 mismatch).
  Unit tests are validated through the direct LLVM clang-cl driver instead.
  Resolution home: toolchain alignment of `tests/FlashCppTest`.
- Pre-existing unity-suite failure
  `SemanticAnalysis:*QueryTracksAnalysisState` reproduces on clean `main`;
  details and suspected shared-static cause live in docs/KNOWN_ISSUES.md.
  Owner: sema query lifecycle.
