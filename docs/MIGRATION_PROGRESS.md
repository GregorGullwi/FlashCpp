# Front-end migration progress

Living state snapshot for
`docs/2026-08-24-front-end-rearchitecture-plan.md`. Each landed migration
pull request overwrites this file in place; this is not a history. Earlier
states are recoverable from git history.

Last updated: 2026-08-25 by branch
`plan-negative-diagnostic-boundaries-2b`

## Position

- Architecture boundary in progress: 0 (diagnosability and measurement)
- Pull request boundary 2B landed: the lexer numeric-literal slice of the
  frozen negative inventory. Ill-formed hexadecimal floating literals without
  a binary exponent ([lex.fcon]) and invalid `u`/`l` integer-suffix
  combinations ([lex.icon]) now lex as `Token::Type::ErrorLiteral`, are
  classified from the spelling by the shared `findNumericLiteralAnomaly`
  helper, and are reported through `DiagnosticEngine`
  (`HexFloatRequiresBinaryExponent` 1101, `InvalidIntegerLiteralSuffix` 1102,
  new block 1101..1199) at the primary-expression choke point without
  throwing. Rejection still flows through the existing `ParseResult` channel;
  accumulated engine diagnostics render once at the `FlashCppMain`
  parse-rejection choke point. Three frozen tests were renamed to
  `_e1101`/`_e1102` filename contracts and mutation-validated.
- `test_default_arg_after_nondefault_fail.cpp` was deleted under the
  incomplete-feature rule: its `[dcl.fct.default]/4` diagnostic fires
  internally but is masked by the declaration-to-expression fallback (see
  `docs/KNOWN_ISSUES.md`). The frozen legacy inventory is rebaselined at 255
  names with updated count and SHA-256 guards in both runners. The seven-test
  internal-failure compatibility is unchanged at 7 against baseline 7,
  direction down, removal boundary 2F.

## Criteria completion

- Explicit exit criteria total: 78 (boundaries 0 through 11)
- Completed: 2/78 (3%)
  - Boundary 0 "diagnostics emitted outside the engine have a baseline and a
    named removal target in architecture boundary 11"
  - Boundary 0 "structured diagnostics can be asserted by tests"
- Advanced, not completed:
  - Boundary 0 "every known architectural defect has a mutation-validated
    regression or a tracked expected failure": the ASAN crash-handler
    ownership defect has a regression, declarator- and literal-family
    diagnostics are filename-pinned and mutation-validated, and the positive
    expected-failure manifest rejects stale stages; the remaining
    architectural regression corpus is still outstanding
  - Boundary 0 "choke-point counters and the remaining static inventories are
    visible in CI on a fixed corpus": the outside-engine counter is enforced
    in Windows CI on a fixed corpus that now includes the encoded literal
    tests; outstanding are the replay, AST-to-IR lookup, codegen-to-parser,
    post-parse typing, and template-routing counters plus the `'$'` static
    inventory (pull request boundary 4), and wiring this check into the
    Ubuntu lane once a Linux-generated baseline is verified there

## Effort estimate

- Implementation effort completed overall: 4-6%, confidence medium

## Remaining work

Replaces the previous remaining-work section entirely on every update.

Next blocker:

- None blocking the next conversion batch locally. PowerShell runner and
  Windows compiler validation remain CI-only in the current environment.

Then, in order:

1. Pull request boundary 2C: convert the constexpr, initialization, and
   constant-expression negative-test slice by shared diagnostic owner.
2. Pull request boundaries 2D through 2F: continue converting the frozen
   legacy negative tests in bounded diagnostic-owner batches; boundary 2F
   deletes `_fail.cpp` classification, both frozen inventories, and the
   seven-test internal-failure compatibility.
3. Preprocessor-directive diagnostics stay unconverted in the frozen
   inventory (`#include_next` file-not-found; recursive macro expansion
   surfacing as a generic parser error) and need their own owner batch or
   deletion review.
4. Pull request boundary 3: first architectural regression slices
   (promotion, namespace-template identity, ambiguous member lookup),
   mutation-validated.
5. Pull request boundary 4: template facade plus the remaining choke-point
   counters and the `'$'` inline-parsing static inventory.

Named follow-ups carried forward:

- Wire `tests/run_migration_counters.ps1` into `ci-ubuntu.yml` after
  generating and verifying the baseline on a Linux build.
- Pre-ICE raw `std::cerr` context dumps at `src/IrGenerator_MemberAccess.cpp`
  emit error text outside both the engine and the counter before throwing
  `InternalError`; decide ownership when ICE reporting moves behind
  `DiagnosticEngine`.
- Unify the ParseResult-channel pointer-to-reference twin at
  `src/Parser_Decl_DeclaratorCore.cpp:477` onto
  `DiagnosticId::PointerToReferenceType` once ParseResult carries structured
  diagnostics.
- Declaration-to-expression fallback masking (KNOWN_ISSUES): any masked
  rejection site must route through the shared declaration dispatch or its
  test is deleted before a structured ID is assigned.
- Blanket `noexcept` on member functions stays deferred until boundaries 5-8
  shrink the exception surface to invariant-only paths.

## Active findings

Current findings only; delete entries when their resolution lands.

- Seven legacy negative tests use the immutable, status-2 compatibility
  inventory. The active count is 7 against baseline 7 and may only fall.
  Details live in `docs/KNOWN_ISSUES.md`; deletion target is boundary 2F.
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
