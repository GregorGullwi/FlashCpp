# Semantic Analysis Status

**Last Updated:** 2026-06-06

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
- **Fallback 4 (`buildCodegenOverloadResolutionArgType` final clause in `IrGenerator_Stmt_Decl.cpp`)**: sema now owns more of the previously non-normalized implicit-constructor/default-member-initializer argument typing path; codegen keeps only the explicit `std::nullopt` "type unknown" handoff and no longer attempts parser/codegen type reconstruction there.
- **Fallback 3 (binary operator LHS/RHS type conversion in `IrGenerator_Expr_Operators.cpp`)**: *not a parser API fallback*. Calls `generateTypeConversion` directly for legitimately uncovered cases (pointer arithmetic and unscoped/scoped enum operands where sema annotations are partial). Intentionally retained.
- codegen no longer contains any `parser_.get_expression_type(...)` calls in the codegen IR-lowering paths that were audited

## NTTP deferred constructor body fix (2026-05-23)

Class-template constructors that reference a non-type template parameter (NTTP) inside
their body were failing with "Symbol 'N' not found during code generation" when the
class also contained a `static constexpr` member (causing the constructor body to be
deferred).

**Root cause (two bugs):**

1. `Parser_Decl_StructEnum.cpp`: when storing a deferred constructor body, `template_param_names`
   was left as `{}`. Fixed by calling `currentTemplateParamNames()` so the replay pass sets
   `hasActiveTemplateParameters() = true` and the template-aware lookup returns
   `TemplateParameterReferenceNode(N)` instead of null.

2. `Parser_Expr_PrimaryExpr.cpp` (`else` at line ~8730): when the identifier is not a
   function-call site and `identifierType` is already `TemplateParameterReferenceNode(N)`,
   the old code unconditionally overwrote `result` with `createBoundIdentifier("N")` =
   `IdentifierNode("N")`. Codegen then failed with "Symbol 'N' not found" because "N" is
   not a runtime symbol. Fixed by: during deferred replay (when `template_param_substitutions_`
   is non-empty), substitute integral NTTPs directly to `NumericLiteralNode`; pointer / reference /
   function-pointer NTTPs (those with `typed_value_identity`) are left as `IdentifierNode` as
   before (their substitution happens via the instantiation-time `substitute_template_params_in_expression`
   pathway).

**Test suite:** 2489 pass / 0 fail (was 2484 before this fix; 5 previously-failing NTTP tests now pass).



1. Continue reducing codegen semantic recovery where sema can be authoritative in non-dependent code.
2. Keep parser/template boundary invariants strict as new template features land.
3. Continue tightening query-state coverage on narrower specialized caches.
4. Keep fallback behavior explicit and limited to genuinely unresolved/dependent flows.
5. **NTTP – pointer/reference substitution in deferred constructor bodies**: pointer-type NTTPs
   (`auto P = &obj`) inside deferred class-template constructor bodies currently fall back to
   `IdentifierNode("P")`, resolved at instantiation time via `substitute_template_params_in_expression`.
   A dedicated path calling the full `trySubstituteValueTemplateParam` logic (handling
   `typed_value_identity`) should be added to the `else` at ~8730 so all NTTP kinds are
   uniformly substituted at parse time.

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
