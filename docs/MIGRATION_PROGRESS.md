# Front-end migration progress

Living state snapshot for
`docs/2026-08-24-front-end-rearchitecture-plan.md`. Each landed migration
pull request overwrites this file in place; this is not a history. Earlier
states are recoverable from git history.

Last updated: 2026-08-30 after pull request boundary 9

## Position

- Architecture boundary in progress: 1 (front-end context, arenas, identities,
  and entities). Pull request boundaries 1 through 9 are complete; architecture
  boundary 1 exit criteria remain open through follow-on boundary-1 work.
- `FrontendContext` owns `DeclarationBuilder`, which publishes `DeclId` /
  `EntityId` into typed `ChunkedVector` arenas keyed by `OwnerId` (namespace
  registry identity), not lexical `ScopeId`. Publication targets are validated
  through `SymbolTable` scope metadata (`resolvePublicationTarget`); lexical
  `ScopeId` is recorded on each `DeclarationRecord`. Namespace/global C++-linkage
  non-template free functions are published through a **shadow path** at
  `Parser::parse_declaration_or_function_definition()` after `SymbolTable::insert`.
  The opaque `TypeId` bridge is telemetry-only until boundary 3A canonical types;
  `SymbolTable` remains lookup and merge authority for the wired family.
- `gChunkedAnyStorage` is a guarded `LegacyAstChunkedAnyVector`. `emplace_back`
  and `ASTNode::emplace_node` reject types outside the compile-time legacy
  allow-list in `LegacyChunkedAnyAllowList.h`; new semantic records must use
  `FrontendContext` typed arenas.
- Persistent scopes still live in `SymbolTable` (cursor exit, `ScopeId` on
  insert and lookup). `FrontendContext` publishes that state for telemetry.
  `ScopeId` is not yet stored on declaration AST nodes.
- Global forwarding into `FrontendContext` is telemetry-only so far (string-
  table entry count and spelling bytes, plus published scope and
  declaration/entity counts); syntax/semantic/IR domain bytes and broader
  per-record counts remain unwired.

## Pull request boundary status (1–9)

| Boundary | Delivered |
|----------|-----------|
| 1 | Diagnostic engine, crash handler, initial conversions |
| 2A–2F | Filename diagnostic contract, legacy `_fail` conversion |
| 3 | Architectural regression probes (tracked expected failures) |
| 4 | `TemplateEngine` facade, migration choke-point counters |
| 5 | `FrontendContext`, strong IDs, scratch arena, arena telemetry |
| 6 | Persistent scopes, `ScopeId` on lookup, first spelling recovery deletion |
| 7 | `DeclarationBuilder` shell, `OwnerId` entity keys, scope-validated publication, initial function merge set |
| 8 | Parser shadow wire for namespace/global C++ free functions, telemetry `TypeId` bridge, `declaration_builder_publish` counter |
| 9 | `ChunkedAnyVector` compile-time legacy-node allow-list guard on `gChunkedAnyStorage` and `ASTNode::emplace_node` |

Pull request boundaries are not the same as architecture boundaries 0–11.
Architecture boundary 0 tracking slices are substantially closed; architecture
boundary 1 is started, not finished.

## Criteria completion

- Explicit exit criteria total: 78 (boundaries 0 through 11)
- Completed: 8/78 (10%)
  - Boundary 0 "diagnostics emitted outside the engine have a baseline and a
    named removal target in architecture boundary 11"
  - Boundary 0 "structured diagnostics can be asserted by tests"
  - Boundary 0 "choke-point counters and the remaining static inventories are
    visible in CI on a fixed corpus"
  - Boundary 1 "IDs cannot be constructed from pointers"
  - Boundary 1 "leaving parser scope does not destroy lookup information"
  - Boundary 1 "declaration merging passes its initial regression set"
  - Boundary 1 "the legacy allocation choke point rejects non-legacy semantic
    node types at compile time"
  - Boundary 1 "no new semantic object is allocated in ChunkedAnyVector" (Decl/
    Entity use typed arenas; compile-time guard now enforces for
    `gChunkedAnyStorage`)
- Advanced, not completed:
  - Boundary 0 "every known architectural defect has a mutation-validated
    regression or a tracked expected failure"
  - Boundary 1 "every template instantiation entry point passes through the
    facade": external callers use `TemplateEngine`; parser-internal paths
    remain direct until boundary 6/8A
  - Boundary 1 "a scratch transaction can allocate declarations and types, fail,
    and leave every committed registry unchanged": proven in doctest; not wired
    to parser tentative parsing
  - Boundary 1 "discarded scratch bytes are measured and bounded": measured;
    no implementation limit yet
  - Boundary 1 "arena bytes, record counts, string-table bytes, and selected
    InlineVector spill counts are reported through FrontendContext": partial
    (scratch, string-table, InlineVector spills, scope count/current ScopeId,
    declaration/entity counts under `--perf-stats`)
  - Boundary 1 remaining exit criteria (full merge rules beyond the initial
    free-function set, full template-facade coverage, `ScopeId` on declarations
    beyond insert/lookup recording, scope storage fully owned by
    `FrontendContext` rather than `SymbolTable`): follow-on boundary-1 work

## Effort estimate

- Implementation effort completed overall: 16-19%, confidence medium

## Remaining work

Replaces the previous remaining-work section entirely on every update.

Next blocker:

- Land canonical function/type identity (architecture boundary 3A) and
  transactional publication (preflight or rollback) before deleting
  `SymbolTable::insert` function redeclaration merge for the wired family.
  Do not make `DeclarationBuilder` sole merge authority on the current
  `matches_signature` interner.

Then, in order:

1. Continue architecture boundary 1: expand shadow wire or merge coverage
   (default arguments, exception specifications, friends, templates) only
   after canonical `TypeId` and transactional publish exist; record `ScopeId`
   on declaration AST nodes; move scope storage into `FrontendContext`; wire
   syntax/semantic/IR domain byte accounting and per-record counts.

Named follow-ups carried forward:

- Before architecture boundary 10A, approve a parser-family routing table for
  the single translation-unit parse entry point.
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
- Telemetry compile-time gates in `MigrationTelemetryConfig.h` default on; set
  individual flags to 0 for shipping builds when ready.
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
- `DeclarationBuilder` rejection after successful `SymbolTable::insert` leaves
  lookup state committed on error paths until transactional publication lands.
  The shadow wire no longer fails parse on builder rejection; SymbolTable stays
  authoritative. Owner: boundary-1 transactional publication follow-up.
- The parser `TypeId` bridge interns via `matches_signature`, which ignores
  nested `FunctionSignature` data (e.g. `void f(void (*)(int))` vs
  `void f(void (*)(double))` can share a builder signature). Owner: boundary 3A
  canonical types; do not delete `SymbolTable` merge on this interner.
