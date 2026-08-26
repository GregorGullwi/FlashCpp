# Front-end migration progress

Living state snapshot for
`docs/2026-08-24-front-end-rearchitecture-plan.md`. Each landed migration
pull request overwrites this file in place; this is not a history. Earlier
states are recoverable from git history.

Last updated: 2026-08-26 by branch
`boundary2c-oneoff-constexpr-diagnostics`

## Position

- Architecture boundary in progress: 0 (diagnosability and measurement)
- Pull request boundary 2C fifth slice landed: the remaining one-off pointer
  reasons carry stable identities. Null-pointer dereference is
  ConstantExpressionNullPointerDereference (1213); detection treats a
  pointer-typed operand with value zero as null because parameter binding
  overwrites the argument's spelling type with the declared pointer type.
  Pointer-plus-pointer addition is ConstantExpressionPointerPlusPointer
  (1214), reachable because auto variable initialization now surfaces a
  structured constant-expression rejection before its generic deduction
  failure when the evaluator already owns the error. Negative-offset
  creation with dereference converted to the existing
  ConstantExpressionPointerCreationOutsideObject (1207). Three frozen tests
  renamed to encoded contracts; all mutation-validated per rule. The frozen
  legacy inventory is rebaselined at 231 names. direct-subscript
  out-of-bounds reads and writes during constant evaluation carry the stable
  identity `ConstantExpressionArrayIndexOutOfBounds` (1212), wired across the
  bound-array subscript, subscript/multi-dimensional assignment,
  materialization (typed, inline, static-member, and multi-dim row paths),
  member-array access, and lvalue-resolution sites. The sites report
  `EvalErrorType::NotConstantExpression`, which the eager global
  constexpr/constinit initializer validator requires before rejecting;
  identifier-array subscripts previously slipped through that validator as
  evaluator-gap failures, so unused out-of-bounds global initializers such as
  `constexpr int bad = arr[5];` were silently accepted with garbage values.
  They now reject. Five encoded regressions pin the family (global init,
  negative index, local write, multi-dim read under 1212; heap struct-array
  element access under existing 1208); all were mutation-validated by cutting
  ID propagation at every tagged site.
- Earlier slices this boundary: arithmetic faults (1201-1205),
  indeterminate-read family (1206), pointer arithmetic and lifetime family
  (1207-1211), direct-subscript out-of-bounds family (1212).
- The full ELF suite ran green end to end on Linux under the hardened
  timeout policy (`runner-timeout-resilience`), confirming the mitigation.
- The frozen legacy inventory is rebaselined at 234 names with updated count
  and SHA-256 guards in both runners. The seven-test internal-failure
  compatibility is unchanged at 7 against baseline 7, direction down, removal
  boundary 2F. pointer arithmetic and
  lifetime violations during constant evaluation carry five stable IDs in
  block 1201..1299 — pointer creation outside the pointee object (1207),
  out-of-bounds access through a computed pointer (1208), subtraction across
  different objects (1209), relational comparison across different objects
  (1210), and use after free (1211). The identity is wired at every evaluator
  site owning those rules: `make_checked_constexpr_pointer_result` formation
  checks, the dereference and arrow read/write paths, the heap-use-after-free
  guards, and the pointer subtraction/relational operators. Error
  classifications and recovery behavior are unchanged; only identity was
  added. Nine frozen tests were renamed to filename-encoded contracts, each
  emitting exactly one occurrence, mutation-validated by cutting ID
  propagation per rule.
- Earlier slices this boundary: arithmetic faults (1201-1205) and the
  indeterminate-read family (1206).
- The frozen legacy inventory is rebaselined at 234 names with updated count
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
    declarator-, literal-, constant-expression-arithmetic,
    indeterminate-read, and pointer-arithmetic/lifetime-family diagnostics
    are filename-pinned and mutation-validated; the remaining architectural
    regression corpus is still outstanding
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

1. Two adjacent evaluator gaps need reduced tests and fixes before IDs can
   be assigned: nested subscripts on global multi-dimensional arrays fail
   with "Array subscript on unsupported expression type", and member-array
   subscripts through a local struct binding fail with "AST node is not an
   expression" instead of reaching any bounds check. With these, boundary 2C
   constant-expression conversion is complete and boundaries 2D-2F follow.
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
- Linux full-suite runs at `-j$(nproc)` flaked with mass `RUNTIME_TIMEOUT`
  under parallel load (341 results in one observed run against the previous
  fixed five-second limit, including `test_minimal_ret42.cpp`; the failing
  set shifted between runs while the same names passed in isolation,
  independent of code state). Mitigated in branch `runner-timeout-resilience`:
  both runners now use a 120 s compile window, a 30 s runtime window, and one
  retry of a timed-out program before classifying failure; deterministic
  timeouts still fail. Residual risk: a host stall longer than one timeout
  plus retry cycle can still flap affected tests. Owner: runner scheduling
  policy. Follow-up: the full suite completed green on Linux under this
  policy on 2026-08-26.
