# Front-end migration progress

Living state snapshot for
`docs/2026-08-24-front-end-rearchitecture-plan.md`. Each landed migration
pull request overwrites this file in place; this is not a history. Earlier
states are recoverable from git history.

Last updated: 2026-08-30 by branch `codex/boundary-2f-zero-iteration-8`

## Position

- Architecture boundary in progress: 0 (diagnosability and measurement)
- The current call-diagnostics batch assigns `NoViableFunctionCall` (1704),
  `NoViableMemberFunctionCall` (1705), `LambdaReturnTypeMismatch` (1706),
  `NonStaticMemberFunctionCall` (1707),
  `NoViableMemberFunctionTemplateCall` (1708), and
  `NoViableCallOperator` (1709) at existing parser rejection choke points.
  It converts seventeen frozen tests covering invalid conversions, hidden
  member overloads, lambda return mismatch, class-scope member calls, and
  callable/member-template failures. No overload decision or recovery path
  was added.
- The current semantic-diagnostics batch assigns stable IDs for auto-return
  mismatches (1013), deleted function and default-constructor calls
  (1321-1322), builtin type rejection (1403-1404), duplicate bases and
  invalid value categories/conversions (1609-1612), and invalid `sizeof...`
  operands (1806) at existing parser and semantic owners.
- The current template/constant-expression batch assigns explicit-instantiation
  IDs (1807-1811), constexpr materialization and evaluation IDs (1221-1223),
  the const-receiver call ID (1710), and the structured-binding protocol ID
  (1613). It converts thirteen more bounded failures without adding replay or
  recovery behavior.
- Iteration 4 assigns `PointerToReferenceType` (1001),
  `DeletedFunctionCall`/`DeletedDefaultConstructorCall` (1321-1322), and
  `NarrowingConversionInListInitialization` (1507) at existing parser and
  lowering rejection owners. It converts nine frozen tests, including three
  existing `NoViableFunctionCall` contracts and one existing
  `InvalidArrayToScalarInitialization` contract. No lookup, replay, recovery,
  or new lowering query was added.
- Iteration 5 assigns `AutoTypeDeductionFailure` (1014),
  `LambdaCaptureNotFound` (1614), and `UndeclaredQualifiedIdentifier` (1615)
  at existing parser and lambda-lowering rejection owners. It converts five
  more frozen tests without adding lookup, replay, recovery, or lowering
  queries.
- Iteration 6 assigns `PreprocessingRejectedInput` (1103),
  `ConstantExpressionMemberNotFound`/`ConstantExpressionSubscriptRequiresArray`
  (1224-1225), `NoMatchingConstructor` (1508), and `UndeclaredIdentifier`
  (1616) at existing preprocessing, evaluator, semantic, and parser owners.
  It converts seven more frozen tests without adding lookup, replay, recovery,
  or lowering queries.
- Iteration 7 assigns `AccessControlViolation` (1617) and
  `AmbiguousQualifiedLookup` (1618) at existing member-access and inline-
  namespace lookup owners. It extends `NoMatchingConstructor` (1508) through
  the existing declaration-lowering helper, and converts three more bounded
  failures to existing `IncompleteSizeofOperand` (1605) and
  `NoViableFunctionCall` (1704) contracts. Eight frozen tests are converted;
  the lookup bridge changes no lookup result or recovery behavior.
- Iteration 8 applies the Boundary 2F stop rule: it removes the 28 remaining
  unsupported fixtures from executable discovery, archives their `.cpp.txt`
  reproducers, removes both frozen inventories and the temporary
  internal-failure compatibility, and removes root `_fail.cpp` classification.
  The migration corpus remains measurable by replacing the deleted concept row
  with the encoded inline-namespace diagnostic test; the legacy-entry count is
  now zero.
- Pull request boundary 2F now includes the explicit-initialization batch on
  branch `codex/boundary-2f-explicit-initialization` (commit `a136a23c`). It
  assigns `AssignmentToConstObject` (1318),
  `ExplicitConstructorCopyInitialization` (1503), and
  `RangeForBeginEndRequired` (1601) at existing bounded semantic owners.
  The operator/access batch on branch
  `codex/boundary-2f-operator-access` (commit `eb4292bf`) adds
  `OperatorOverloadNotFound` (1319), `DeletedOperatorFunction` (1320), and
  the bounded pointer/subscript validity IDs 1602-1604.
- The existing-diagnostic inventory batch on branch
  `codex/boundary-2f-existing-diagnostics` (commit `17cff091`) converts twelve
  already-structured legacy tests to encoded successors and removes one
  resolved entry from the temporary internal-failure compatibility inventory.
  The constructor-ambiguity batch on the current branch (commit `0e4bdb48`)
  adds `AmbiguousConstructorCall` (1504) at the existing overload-resolution
  choke points and converts three more frozen tests. No replay, lookup,
  recovery, or lowering query was added for these conversions.
- The static-member constructor batch on the current branch extends the same
  `AmbiguousConstructorCall` (1504) contract through constructor-call
  normalization, converting `test_static_member_ctor_overload_ambiguity` and
  removing its former missing-resolved-constructor compatibility entry. No
  fallback resolution or new semantic query was added.
- The current declarator-constraint batch assigns
  `DecltypeAutoCvQualifier` (1004), `DecltypeAutoPointerOrReference` (1005),
  `DecltypeAutoStructuredBinding` (1006), and `ParameterPackDataMember` (1007)
  at existing parser rejection points and converts six frozen tests. No
  `ParseResult` ownership or semantic fallback was added.
- The current overload-resolution batch assigns `AmbiguousFunctionCall` (1701)
  at the two existing ordinary-call parser rejection points and converts four
  frozen tests. It adds no new overload decision or recovery path.
- The current declaration-constraint batch assigns
  `DllImportConstexprConflict` (1505) and `DllImportDataDefinition` (1506) at
  existing parser rejection points and converts three frozen tests. It adds no
  new linkage or initialization decision.
- The current template phase-1 batch assigns
  `NonDependentNameNotDeclaredBeforeTemplateDefinition` (1801) at the two
  existing delayed template-body rejection points and converts three frozen
  tests. It adds no replay or lookup behavior.
- The current call-operator batch assigns `AmbiguousCallOperator` (1702) at
  the existing concrete postfix-call rejection point and converts two frozen
  tests. It adds no new overload-resolution behavior.
- The current partial-specialization batch assigns
  `PartialSpecializationParameterListInvalid` (1802) at the existing template
  parameter-list rejection point and converts two frozen tests. It adds no
  template matching or recovery behavior.
- The current namespace/structured-binding batch assigns
  `InlineNamespaceNestedPrefix` (1008), `InlineNamespaceReopenAsInline` (1009),
  and `StructuredBindingStorageClass` (1010) at existing parser rejection
  points and converts three frozen tests. It adds no scope or binding recovery.
- The current hidden-friend batch assigns
  `HiddenFriendCallWithoutAssociatedArgument` (1803) at the existing ordinary
  call rejection points and converts two frozen tests. It adds no lookup or
  recovery behavior.
- The current incomplete-`sizeof` batch assigns `IncompleteSizeofOperand`
  (1605) at the existing parser rejection point and converts two frozen tests.
  It adds no type-completeness recovery.
- The current constant-expression batch assigns the existing evaluator/parser
  rejection points to `DesignatedInitializerAfterPositional` (1011),
  `DesignatedInitializerOutOfOrder` (1012), and
  `ConstantExpressionConstCastTypeChange`/`ConstantExpressionNarrowingConversion`/
  `ConstantExpressionNonConstexprCall`/`ConstantExpressionThrow`/
  `ConstantExpressionHeapAllocationLeak` (1215-1220), converting ten frozen
  tests. It adds no evaluator fallback or initializer recovery.
- The current parser/lookup batch assigns `ReferenceNonTypeTemplateParameterUnsupported`
  (1805), `DuplicateDeclaration` (1606), `OverrideSpecifierNoBase` (1607),
  `OverrideFinalFunction` (1608), and `AmbiguousMemberFunctionCall` (1703) at
  existing rejection points, converting five frozen tests. It adds no lookup
  or finalization recovery.
- Earlier completed boundary-2 slices cover declarator and source-structure
  diagnostics in 2B, followed in 2C by constant-expression arithmetic faults
  (1201-1205), the indeterminate-read family (1206), pointer arithmetic and
  lifetime (1207-1211), direct-subscript bounds checking (1212),
  null-pointer dereference (1213), and pointer-plus-pointer (1214).
  Boundary 2D then assigned the floating-point operator family (1301-1304),
  followed by operator ambiguity, operator signatures, deleted assignment,
  and immediate-invocation diagnostics (1305-1315).
- The full Windows-suite validation on 2026-08-30 covered 2,959 regular tests,
  253 encoded negative tests, and one multi-translation-unit case; all
  compile/link phases passed, with no crashes, runtime mismatches, or
  negative-contract failures. Five tracked positive expected failures matched.
  The latest Linux full-suite run covered 2,929 single-file tests, 281
  negative tests, one multi-translation-unit case, and five tracked positive
  expected failures; it had no crashes or mismatches.
- The filename-encoded negative inventory is now the only negative-test
  contract; its current legacy-entry count is zero.

## Criteria completion

- Explicit exit criteria total: 78 (boundaries 0 through 11)
- Completed: 2/78 (3%)
  - Boundary 0 "diagnostics emitted outside the engine have a baseline and a
    named removal target in architecture boundary 11"
  - Boundary 0 "structured diagnostics can be asserted by tests"
- Advanced, not completed:
  - Boundary 0 "every known architectural defect has a mutation-validated
    regression or a tracked expected failure": the ASAN crash-handler defect,
    declarator-, `decltype(auto)`-, parameter-pack-, literal-,
    constant-expression-arithmetic,
    indeterminate-read, pointer-arithmetic/lifetime-family, direct
    multidimensional/member-subscript, and scoped-enum (1401/1402, sema-owned)
    diagnostics are filename-pinned and mutation-validated; floating-point
    modulo, bitwise-compound, shift, and plain-bitwise operator diagnostics
    have also entered the 2D encoded corpus, and the operator overload
    ambiguity diagnostic (1305) and static-`constexpr` member initializer
    diagnostic (1502), the complete converted operator-signature family
    (1306-1312), deleted copy/move assignment diagnostics (1313/1314),
    non-constant immediate invocation diagnostics (1315), and the
    explicit-initialization, declarator-constraint, semantic-validity, and
    ordinary-call and call-operator ambiguity, dllimport-constraint, template
    phase-1, partial-specialization, namespace, and structured-binding
    diagnostics (1001, 1004-1014, 1103, 1201-1225, 1318-1322, 1503-1508,
    1601-1608, 1614-1618, 1701-1704, 1801-1805),
    including the static-member constructor path, are
    mutation-validated on the same terms;
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

- There is no cross-platform validation blocker for the current 2F snapshot:
  the full Windows suite is green and the frozen inventory has reached zero;
  the current coordination target is complete.

Then, in order:

1. Pull request boundaries 2D through 2F: the bounded diagnostic-owner batches
   and the Boundary 2F cleanup are complete. The diagnostic-contract durability
   and legacy-investment stop rule remains in force for later migrations: a
   batch cannot extend replay, parser-owned semantic work, identity recovery,
   AST-to-IR lookup, or lowering recovery merely to convert a test.
2. Pull request boundary 3: first architectural regression slices
   (promotion, namespace-template identity, ambiguous member lookup),
   mutation-validated. The `auto`, constexpr, and template-deduction promotion
   probes plus the two namespace-template probes are tracked positive expected
   failures; the `sizeof`, overload-ranking, and ambiguous-member probes
   currently pass. This advances architecture boundary 0 only
   and does not complete boundaries 4, 5, or 6.
3. Pull request boundary 4: template facade plus the remaining choke-point
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
