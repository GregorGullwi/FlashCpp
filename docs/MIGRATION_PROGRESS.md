# Front-end migration progress

Living state snapshot for
`docs/2026-08-24-front-end-rearchitecture-plan.md`. Each landed migration
pull request overwrites this file in place; this is not a history. Earlier
states are recoverable from git history.

Last updated: 2026-08-26 by branch
`boundary2c-indeterminate-read-diagnostics`

## Position

- Architecture boundary in progress: 0 (diagnosability and measurement)
- Pull request boundary 2C second slice landed: reads of indeterminate
  storage during constant evaluation carry the stable identity
  `ConstantExpressionIndeterminateValueRead` (new number 1206 in block
  1201..1299). Default-initialized scalars, aggregate members without default
  member initializers, and bare `new` objects all violate the same
  core-constant-expression requirement, so one ID owns the family. It
  originates at the three shared evaluator fault sites —
  `validateConstexprRead` (heap, array-element, member, and bound-identifier
  reads), compound assignment on an indeterminate target, and `++`/`--` on an
  indeterminate target — through the shared
  `ConstExpr::indeterminateValueReadError` factory, travels on
  `EvalResult::diagnostic_id`, and reports through the existing terminal
  static_assert rejection without changing recovery behavior. Seven frozen
  tests were renamed to `_e1206` filename contracts; each emits exactly one
  `#1206` occurrence and was mutation-validated by cutting ID propagation at
  the shared factory.
- The frozen legacy inventory is rebaselined at 243 names with updated count
  and SHA-256 guards in both runners. The seven-test internal-failure
  compatibility is unchanged at 7 against baseline 7, direction down, removal
  boundary 2F.

## Criteria completion

- Explicit exit criteria total: 78 (boundaries 0 through 11)
- Completed: 2/78 (3%)
  - Boundary 0 "diagnostics emitted outside the engine have a baseline and a
    named removal target in architecture boundary 11"
  - Boundary 0 "structured diagnostics can be asserted by tests"
- Advanced, not completed:
  - Boundary 0 "every known architectural defect has a mutation-validated
    regression or a tracked expected failure": the ASAN crash-handler defect,
    declarator-, literal-, constant-expression-arithmetic, and
    indeterminate-read-family diagnostics are filename-pinned and
    mutation-validated; the remaining architectural regression corpus is
    still outstanding
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

- None blocking the next conversion batch locally. PowerShell runner and
  Windows compiler validation remain CI-only in the current environment.

Then, in order:

1. Pull request boundary 2C continuation slices, each by shared diagnostic
   owner within constant-expression evaluation: the pointer arithmetic and
   lifetime family first (negative pointer creation, out-of-bounds
   dereference, different-array comparison, freed heap access), then the
   remaining one-off reasons.
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

- Pin `ConstantExpressionSignedIntegerOverflow` (1205) with an encoded
  regression once the evaluator tracks promoted operand widths; it currently
  evaluates integer arithmetic in 64-bit so 32-bit overflow goes undetected
  (see `docs/KNOWN_ISSUES.md`).
- Support message-less `static_assert` ([stmt.assert]/1) before converting
  static_assert-owned diagnostics that standard headers hit through the
  message-less form (see `docs/KNOWN_ISSUES.md`).
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
- Declaration-parse errors masked by the top-level expression-statement
  fallback: any masked rejection site must route through the shared
  declaration dispatch or its test is deleted before a structured ID is
  assigned (see `docs/KNOWN_ISSUES.md`).
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
- Linux full-suite runs at `-j$(nproc)` flake: the fixed five-second runtime
  limit trips under 24-way parallel load even for instant-return programs
  (341 `RUNTIME_TIMEOUT` results in one observed run, including
  `test_minimal_ret42.cpp`; the failing set shifts between runs and the same
  names pass in isolation and in smaller batches, independent of code state).
  Treat mass `RUNTIME_TIMEOUT` on this lane as scheduler noise until the
  runner gains load-aware timeouts or a bounded default job count. Owner:
  runner timeout/scheduling policy.
