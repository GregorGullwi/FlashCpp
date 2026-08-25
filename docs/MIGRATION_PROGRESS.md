# Front-end migration progress

Living state snapshot for
`docs/2026-08-24-front-end-rearchitecture-plan.md`. Each landed migration
pull request overwrites this file in place; this is not a history. Earlier
states are recoverable from git history.

Last updated: 2026-08-25 by branch
`boundary2-filename-diagnostics-manifest`

## Position

- Architecture boundary in progress: 0 (diagnosability and measurement)
- Last completed slice: pull request boundary 2 item — file-based diagnostic
  assertions: `_fail.cpp` tests can pin diagnostics through
  `// expected-diag:` comments carrying severity, stable `[Name#number]` ID,
  line, and column; both runners verify strict set equality against the plain
  rendered output (decorated logger copies are structurally excluded), fail
  as `DIAG_MISMATCH` naming missing and unexpected entries on any drift, and
  stay byte-compatible for tests without assertions. Shared helpers live in
  `RunnerCommon.ps1`/`runner_common.sh` with mutation-validated self-tests,
  parallel worker runspaces receive the helpers explicitly, and the three
  declarator-family `_fail` regressions converted in the DiagnosticEngine
  slice now assert their exact diagnostics, including the attached note

## Criteria completion

- Explicit exit criteria total: 78 (boundaries 0 through 11)
- Completed: 2/78 (3%)
  - Boundary 0 "diagnostics emitted outside the engine have a baseline and a
    named removal target in architecture boundary 11"
  - Boundary 0 "structured diagnostics can be asserted by tests"
- Advanced, not completed:
  - Boundary 0 "every known architectural defect has a mutation-validated
    regression or a tracked expected failure": the ASAN crash-handler
    ownership defect has a mutation-validated regression and the three
    declarator-family diagnostics are assertion-pinned; the remaining
    architectural regression corpus and the named expected-failure manifest
    with stale-entry detection are still outstanding
  - Boundary 0 "choke-point counters and the remaining static inventories are
    visible in CI on a fixed corpus": the outside-engine counter is enforced
    in Windows CI on a fixed corpus; outstanding are the replay, AST-to-IR
    lookup, codegen-to-parser, post-parse typing, and template-routing
    counters plus the `'$'` static inventory (pull request boundary 4), and
    wiring this check into the Ubuntu lane once a Linux-generated baseline is
    verified there

## Effort estimate

- Implementation effort completed overall: 3-5%, confidence medium

## Remaining work

Replaces the previous remaining-work section entirely on every update.

Next blocker:

- None blocking.

Then, in order:

1. Pull request boundary 2A: replace inline negative-test assertions with
   filename-encoded diagnostic ID multisets, distinguish clean source rejection
   from internal compiler or driver failure, freeze the legacy `_fail.cpp`
   inventory, migrate the three structured declarator-family tests, and add the
   separate named expected-failure manifest with stale-entry detection.
2. Pull request boundaries 2B through 2F: convert the frozen legacy negative
   tests in bounded diagnostic-owner batches. Each batch assigns stable IDs at
   shared compiler emission sites and renames only its exact inventory slice;
   boundary 2F deletes `_fail.cpp` classification and the frozen inventory.
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

- The unity doctest target's MSBuild ClangCL configuration crashes the clang
  frontend against the VS18 STL headers (LLVM 20.1 vs STL 14.51 mismatch).
  Unit tests are validated through the direct LLVM clang-cl driver instead.
  Resolution home: toolchain alignment of `tests/FlashCppTest`.
- Pre-existing unity-suite failure
  `SemanticAnalysis:*QueryTracksAnalysisState` reproduces on clean `main`;
  details and suspected shared-static cause live in docs/KNOWN_ISSUES.md.
  Owner: sema query lifecycle.
