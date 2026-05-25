# Semantic Analysis Status

**Last Updated:** 2026-05-25

This is the consolidated status document for sema-related planning and architecture notes.

## Current pipeline

FlashCpp currently follows a parse -> sema -> IR pipeline:

- parser and sema are both created before parse (`src/FlashCppMain.cpp`)
- parser runs first, then `SemanticAnalysis::run()`
- IR lowering (`AstToIr`) requires completed post-parse semantic normalization before codegen starts

## Ownership model (current)

### Parser owns

- syntax construction and AST shape
- template-instantiation mechanics and late materialization triggers
- symbol creation and parse-time lookup surfaces

### Semantic analysis owns

- post-parse semantic normalization (`SemanticAnalysis::run`)
- semantic type/query surfaces used by parser/codegen via `ParserSemanticServices`
- explicit query-state APIs (`NotYetAnalyzed`, `AnalyzedAbsent`, `Available`)
- sema-owned invariants for normalized bodies (missing required semantic facts should fail loudly)

### Constexpr / template evaluation consumes

- parser-attached sema services where parser-owned contexts require semantic facts
- explicit sema-backed evaluation context construction for parser-owned flows

### Codegen still contains controlled fallback in some paths

- sema query APIs are consumed first in multiple lowering paths
- parser fallback still exists in some places (for unresolved / non-normalized / legacy compatibility cases)

## Verified codebase state

- `SemanticAnalysis.cpp` no longer uses direct `parser_.get_expression_type(...)`
- `ParserSemanticServices` exposes parser-phase-safe APIs and lifecycle checks
- query-state APIs are in use (`NotYetAnalyzed`, `AnalyzedAbsent`, `Available`)
- `AstToIr::buildCodegenOverloadResolutionArgType(...)` now consumes sema overload-argument typing APIs first and no longer performs codegen-local legacy type reconstruction
- `AstToIr::generateMemberFunctionCallIr(...)` now requires sema-owned callable receiver typing for `operator()` in normalized bodies and no longer uses codegen symbol-table receiver type recovery in that path
- `AstToIr::getCallExpressionReturnType(...)` now requires a sema-owned call-result type and hard-fails if the semantic query is missing or unusable
- builtin `++/--` lowering now requires sema-owned operand/object type queries instead of codegen parser expression-type fallback
- builtin `++/--` member-object pointer-depth handling now also consumes sema expression-type queries directly and no longer performs codegen symbol-table / implicit-`this` object-type recovery before sema
- `AstToIr::generateFunctionCallIr(...)` inline-always argument typing now uses sema overload-resolution argument type APIs and no longer falls back to parser expression typing in that path
- `AstToIr::generateMemberFunctionCallIr(...)` now requires sema-owned receiver typing for call-expression receivers in normalized bodies and no longer uses parser expression-type recovery there
- **Fallback 1 (`resolveStructTypeFromReceiverNode` in `IrGenerator_Call_Indirect.cpp`)**: removed parser identifier/static-cast recovery; receiver struct typing now comes from sema query state only (normalized bodies hard-fail on missing/unusable semantic facts, non-normalized flows receive `std::nullopt` and continue existing downstream resolution).
- **Fallback 2 (call-return receiver recovery in `IrGenerator_Call_Indirect.cpp`)**: confirmed dead — the `allow_parser_type_recovery` guard block never fired in any test. Removed the dead guard; execution falls through to the existing type-normalization code.
- `SemanticAnalysis::normalizeStructDeclaration(...)` now normalizes non-static member default initializers under an implicit-`this` member context, so unqualified member/static-member identifiers in those expressions receive sema-owned type information.
- `AstToIr::visitRangedForStatementNode(...)` now hard-fails normalized bodies on missing/unusable sema range-expression query state and relies on sema query-state typing plus sema-owned `resolved_range_type`; member-access reconstruction remains as a non-normalized compatibility path.
- **Constructor annotation hard-fail (IrGenerator_Stmt_Decl.cpp)**: Converted two `FLASH_LOG(Warning, ...)` / fallthrough paths to `throw InternalError(...)` in sema-normalized bodies:
  - Brace-init path (~line 1487): when `require_sema_resolved_ctor` is true and `init_list.resolved_constructor()` is null — now throws `InternalError("Sema did not annotate brace-init constructor for normalized body")`.
  - Direct constructor call path (~line 2273): when `require_sema_resolved_ctor` is true and `direct_ctor->resolved_constructor()` is null — now throws `InternalError("Sema did not annotate constructor for normalized body")`.
  Both paths were probe-verified against the full test suite (2494 pass / 181 expected-fail) with a temporary probe throw before conversion, confirming sema already annotates all normalized bodies.
- **Constructor overload-arg fallback policy refactor (IrGenerator_Stmt_Decl.cpp)**: Catch/suppress behavior for sema-owned overload-argument typing now lives in a shared helper (`tryBuildCodegenOverloadResolutionArgType(...)`) instead of duplicated local `try/catch` blocks. `getSameTypeConstructorPreference(...)` and brace-init constructor type-collection now both use the shared helper, and sema-normalized bodies still hard-fail identifier-based missing sema overload-arg typing while preserving explicit non-identifier compatibility behavior.
- **Static storage global variable path (~line 830)**: Verified that `normalizeTopLevelNode` for `VariableDeclarationNode` calls `normalizeExpression` → `tryAnnotateConstructorCallArgConversions`, so `ctor_call.resolved_constructor()` is already populated by sema first. The type-based and arity fallbacks at lines 811–834 are only reached when the constexpr evaluator fails AND sema cannot uniquely resolve; they remain as legitimate non-normalized-body fallbacks.
- **Return-statement converting-constructor codegen fallback (fixed 2026-05-23)**: The `!sema_applied_conversion && operands.type_index != return_type_spec.type_index()` block in `visitReturnStatementNode` no longer fires for template nested-type same-type returns. Fix: (1) `isSameTypeConstructorCallInitialization` now compares by struct name in addition to canonical type ID, so sema correctly identifies same-type returns even when TypeIndex slots differ; (2) codegen `visitReturnStatementNode` normalizes `operands.type_index` to `current_function_return_type_index_` when both TypeInfos share the same name. Regression test: `test_template_nested_type_return_direct_ret42.cpp`.
- **Arity fallback at ~line 2363 (direct constructor call)**: now explicitly guarded behind `!sema_normalized_current_function_`, so sema-normalized bodies no longer take codegen arity recovery in this path; it remains a compatibility fallback for non-normalized bodies only.
- **Variable-init arithmetic fallback tightening (IrGenerator_Stmt_Decl.cpp)**: sema-normalized bodies now require sema-owned implicit conversion annotations for all standard arithmetic variable-initialization conversions, including integer→`bool`; the prior bool carve-out in the codegen fallback guard was removed.
- **Fallback 3 (binary operator LHS/RHS type conversion in `IrGenerator_Expr_Operators.cpp`)**: *not a parser API fallback*. Calls `generateTypeConversion` directly for legitimately uncovered cases (pointer arithmetic and unscoped/scoped enum operands where sema annotations are partial). Intentionally retained.
- **Local enum type lookup hardening (parser)**: `lookupTypeInCurrentContext(...)` now prefers active-scope enum declarations and, in function/block scopes, prefers the most recent matching enum `TypeInfo` entry. This keeps `TypeSpecifierNode` enum type indices concrete in local shadowing cases (no `TypeIndex{0, Enum}` fallback in this path). Regression: `test_local_enum_shadow_type_lookup_ret0.cpp`.
- **Scoped enum comparison sema coverage**: `SemanticAnalysis::tryAnnotateBinaryOperandConversions(...)` now annotates same-type scoped-enum comparison operand promotions; codegen now hard-fails sema-normalized bodies if those promotions are missing instead of silently taking the enum fallback.
- `AstToIr::visitSizeofNode(...)` now resolves `sizeof(member_access)` member sizing through `resolveMemberAccessType(...)` (sema-owned member resolution first) and no longer scans instantiated type names (`base`, `base_...`, `base$...`) as a codegen-side recovery path.
- `AstToIr::generateMemberFunctionCallIr(...)` now compares selected member parameter types using sema logical struct-type identity (not only raw `TypeIndex` equality), reducing template-instantiation cases that previously fell through to viable-overload metadata recovery during member-call dispatch setup.
- **`typeid(expr)` sema-owned operand classification (`SemanticAnalysis.cpp`, `IrGenerator_NewDeleteCast.cpp`)**: codegen now consumes a dedicated sema query for `typeid` operand classification (query state, canonical operand type identity, glvalue-ness, and runtime-RTTI requirement) instead of reconstructing that policy locally. Normalized bodies hard-fail if classification would require legacy compatibility recovery; non-normalized flows keep the explicit compatibility fallback behind the sema query boundary.
- **Ordinary direct-call fallback reasons (`SemanticAnalysis.cpp`, `IrGenerator_Call_Direct.cpp`)**: the prior coarse `unresolved_call_args_` escape hatch has been replaced with explicit sema-recorded direct-call fallback reasons. Resolved direct-call lowering no longer contains a real codegen-side lookup recovery block: the declaration-address overload rescan, mangled-symbol retry, qualified-owner member lookup, qualified-static-member scanning, template-type-parameter-qualified static-call recovery, dependent base-template qualified-call remap, and stale precomputed pattern-owner remap have all been deleted from `IrGenerator_Call_Direct.cpp` because sema-side owner/member recovery and sema-owned direct-call targets already cover those cases. The remaining fallback-reason contract is now only a diagnostic/compatibility boundary around normalized misses and argument-conversion exceptions while the last sema-side gaps are burned down.
- codegen no longer contains any `parser_.get_expression_type(...)` calls in the codegen IR-lowering paths that were audited

## Active backlog (high level)

1. Continue reducing codegen semantic recovery where sema can be authoritative in non-dependent code.
2. Keep parser/template boundary invariants strict as new template features land.
3. Continue tightening query-state coverage on narrower specialized caches.
4. Keep fallback behavior explicit and limited to genuinely unresolved/dependent flows.

## Next recommended slice

- **Ordinary direct-call fallback burn-down should continue next**: the next highest-value cleanup is eliminating the remaining explicit direct-call fallback reasons one by one until sema-normalized bodies no longer need even the residual compatibility/exception contract around direct-call misses. See `docs/2026-04-04-codegen-name-lookup-investigation.md` Phase 1 for the detailed roadmap and exit criteria.

## Related documents

- Template/materialization roadmap: `docs/2026-04-08-template-instantiation-materialization-plan.md`
- Fallback inventory: `docs/2026-04-27-fallback-comments-audit.md`
- Codegen name-lookup debt: `docs/2026-04-04-codegen-name-lookup-investigation.md`

## Legacy sema docs

The following files are intentionally kept as short pointers to this consolidated status:

- `docs/IMPLICIT_CAST_SEMA_PLAN.md`
- `docs/2026-03-12_IMPLICIT_CAST_SEMA_PLAN.md`
- `docs/2026-03-21_PARSER_TEMPLATE_SEMA_BOUNDARY_PLAN.md`
- `docs/2026-04-06-template-constexpr-sema-audit-plan.md`
- `docs/2026-05-16-sema-first-class-plan.md`
