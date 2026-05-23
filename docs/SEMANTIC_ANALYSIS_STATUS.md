# Semantic Analysis Status

**Last Updated:** 2026-05-24

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
- **Static storage global variable path (~line 830)**: Verified that `normalizeTopLevelNode` for `VariableDeclarationNode` calls `normalizeExpression` → `tryAnnotateConstructorCallArgConversions`, so `ctor_call.resolved_constructor()` is already populated by sema first. The type-based and arity fallbacks at lines 811–834 are only reached when the constexpr evaluator fails AND sema cannot uniquely resolve; they remain as legitimate non-normalized-body fallbacks.
- **Converting constructor fallback (~line 2593, copy-init path in `IrGenerator_Stmt_Decl.cpp`)**: Probe-verified against the full test suite (2494 pass / 181 expected-fail) — the block never fired for any sema-normalized body. Added a hard-fail guard: `if (sema_normalized_current_function_) throw InternalError("Sema did not annotate converting constructor for normalized body")` at the top of the block. The existing type-based and arity-based fallbacks remain as legitimate non-normalized-body paths.
- **Arity fallback at ~line 2363 (direct constructor call)**: This is unreachable from normalized-body paths (covered by the new hard-fail at ~2273). Remains as a valid codegen-time fallback for non-normalized bodies.
- **Fallback 3 (binary operator LHS/RHS type conversion in `IrGenerator_Expr_Operators.cpp`)**: *not a parser API fallback*. Calls `generateTypeConversion` directly for legitimately uncovered cases (pointer arithmetic and unscoped/scoped enum operands where sema annotations are partial). Intentionally retained.
- `AstToIr::visitSizeofNode(...)` now resolves `sizeof(member_access)` member sizing through `resolveMemberAccessType(...)` (sema-owned member resolution first) and no longer scans instantiated type names (`base`, `base_...`, `base$...`) as a codegen-side recovery path.
- codegen no longer contains any `parser_.get_expression_type(...)` calls in the codegen IR-lowering paths that were audited

## Active backlog (high level)

1. Continue reducing codegen semantic recovery where sema can be authoritative in non-dependent code.
2. Keep parser/template boundary invariants strict as new template features land.
3. Continue tightening query-state coverage on narrower specialized caches.
4. Keep fallback behavior explicit and limited to genuinely unresolved/dependent flows.

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
