# Front-end migration progress

Living state snapshot for
`docs/2026-08-24-front-end-rearchitecture-plan.md`. Each landed migration
pull request overwrites this file in place; this is not a history. Earlier
states are recoverable from git history.

Last updated: 2026-08-28 by branch `boundary-2f-legacy-cleanup`

## Position

- Architecture boundary in progress: 0 (diagnosability and measurement)
- Pull request boundary 2F continues past the scoped-enum sema batch and the
  operator overload ambiguity batch: the four frozen
  `test_operator_ambiguity*` / `test_operator_member_free_exact_ambiguous`
  legacy negative tests converted to structured diagnostics through the single
  bounded owner `AstToIr::generateBinaryOperatorIr` (both the generic binary
  ambiguity site and the struct-assignment `operator=` ambiguity site).
  New ID: `AmbiguousOperatorOverload` (1305) in the operator family
  (1301..1399). The scoped-enum batch before it added
  `ScopedEnumImplicitConversion` (1401) and `ScopedEnumBinaryOperand` (1402)
  in the implicit enum conversion family (1401..1499) through the centralized
  sema owners (`SemanticAnalysis::diagnoseScopedEnumConversion`,
  `SemanticAnalysis::diagnoseScopedEnumBinaryOperands`, and the
  constructor-argument no-arity-match site).
- The current 2F batch converts the four frozen static-`constexpr` member
  initializer failures to `ConstexprStaticMemberInitializerNotConstant`
  (1502). The existing parser validation and AST-to-IR rejection choke points
  now report the same initialization-family ID, and the four tests use
  `_e1502` filenames.
- Earlier completed boundary-2 slices cover declarator and source-structure
  diagnostics in 2B, followed in 2C by constant-expression arithmetic faults
  (1201-1205), the indeterminate-read family (1206), pointer arithmetic and
  lifetime (1207-1211), direct-subscript bounds checking (1212),
  null-pointer dereference (1213), and pointer-plus-pointer (1214).
  Boundary 2D then assigned `FloatingPointModuloOperator` (1301),
  `FloatingPointBitwiseCompoundAssignment` (1302),
  `FloatingPointShiftOperator` (1303), and
  `FloatingPointBitwiseOperator` (1304) across the direct, compound,
  constexpr, global, local, member, and indirect paths covered by their
  regressions. Boundary 2E adds `AmbiguousOperatorOverload` (1305) for the
  ambiguous binary operator overload resolution owner.
- The last recorded full Windows-suite validation is the boundary-2C run:
  2,946 regular tests, 265 negative tests, and one multi-translation-unit case;
  no crashes or mismatches occurred. The 2D and 2E slices still require their
  own next full-suite snapshot.
- The frozen legacy inventory is rebaselined at 207 names. The seven-test
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
    regression or a tracked expected failure": the ASAN crash-handler defect,
    declarator-, literal-, constant-expression-arithmetic,
    indeterminate-read, pointer-arithmetic/lifetime-family, direct
    multidimensional/member-subscript, and scoped-enum (1401/1402, sema-owned)
    diagnostics are filename-pinned and mutation-validated; floating-point
    modulo, bitwise-compound, shift, and plain-bitwise operator diagnostics
    have also entered the 2D encoded corpus, and the operator overload
    ambiguity diagnostic (1305) and static-`constexpr` member initializer
    diagnostic (1502) are mutation-validated on the same terms;
    the remaining architectural regression corpus is still outstanding
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

- None known locally. The current 2F state still needs a full Windows-suite
  run before the next migration snapshot may claim validation.

Then, in order:

1. Pull request boundaries 2D through 2F: continue converting the frozen
   legacy negative tests in bounded diagnostic-owner batches. Apply the
   diagnostic-contract durability and legacy-investment stop rule: attaching a
   stable ID at an existing bounded owner is allowed, but a batch cannot extend
   replay, parser-owned semantic work, identity recovery, AST-to-IR lookup, or
   lowering recovery merely to convert a test. Boundary 2F still deletes
   `_fail.cpp` classification, both frozen inventories, and the seven-test
   internal-failure compatibility unless a later approved roadmap amendment
   moves a named blocked slice and all of its cleanup targets together.
2. Preprocessor-directive diagnostics stay unconverted in the frozen
   inventory (`#include_next` file-not-found; recursive macro expansion
   surfacing as a generic parser error) and need their own owner batch or
   deletion review.
3. Pull request boundary 3: first architectural regression slices
   (promotion, namespace-template identity, ambiguous member lookup),
   mutation-validated. The `auto`, constexpr, and template-deduction promotion
   probes plus the two namespace-template probes are tracked positive expected
   failures; the `sizeof`, overload-ranking, and ambiguous-member probes
   currently pass. This advances architecture boundary 0 only
   and does not complete boundaries 4, 5, or 6.
4. Pull request boundary 4: template facade plus the remaining choke-point
   counters and the `'$'` inline-parsing static inventory.

Named follow-ups carried forward:

- Before architecture boundary 10A, approve a parser-family routing table for
  the single translation-unit parse entry point. Boundaries 10A through 10F
  now separate indexed token input, parser transactions, syntax-only
  declarations, syntax-only expressions and statements, bounded parser control
  flow, and deletion of the parser service locator; no family may be routable
  to both legacy and migrated parsers.
- Pin `ConstantExpressionSignedIntegerOverflow` (1205) with an encoded
  regression once the evaluator tracks promoted operand widths; it currently
  evaluates integer arithmetic in 64-bit so 32-bit overflow goes undetected
  (see `docs/KNOWN_ISSUES.md`).
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
