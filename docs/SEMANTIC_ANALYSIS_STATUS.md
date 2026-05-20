# Semantic Analysis Status

**Last Updated:** 2026-05-20

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
- `AstToIr::generateMemberFunctionCallIr(...)` now keeps `operator()` receiver type recovery on parser-only for non-normalized bodies; normalized bodies no longer use parser expression-type fallback in that path
- codegen still has parser expression-type fallback in selected IR paths (`src/IrGenerator_*.cpp`)

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
