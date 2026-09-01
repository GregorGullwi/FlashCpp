# Front-end migration progress

Living state snapshot for
`docs/2026-08-24-front-end-rearchitecture-plan.md`. Each landed migration
pull request overwrites this file in place; this is not a history. Earlier
states are recoverable from git history.

Last updated: 2026-09-01 after pull request boundary 32

## Position

- Architecture boundary in progress: 1 (front-end context, arenas, identities,
  and entities). Pull request boundaries 1 through 32 are landed.
  Architecture boundary 1 exit criteria remain open through
  follow-on boundary-1 work.
- `FrontendContext` owns `DeclarationBuilder`, which publishes `DeclId` /
  `EntityId` into typed `ChunkedVector` arenas keyed by `OwnerId` (namespace
  registry identity), not lexical `ScopeId`. Publication targets are validated
  through `SymbolTable` scope metadata (`resolvePublicationTarget`); lexical
  `ScopeId` is recorded on each `DeclarationRecord`. Namespace/global C++-linkage
  non-template free functions are published through a **shadow path** at
  `Parser::parse_declaration_or_function_definition()` after `SymbolTable::insert`.
  The path preflights with `prepareFunctionPublication`, commits through
  `PublicationTransaction` mark/rollback, and leaves the `SymbolTable`
  insertion in place when publication rejects. `SymbolTable` remains lookup
  and merge authority; the opaque TypeId bridge is telemetry-only until
  boundary 3A canonical types and must not diagnose or erase lookup state.
  Parser shadow publication has one transactional adapter:
  `commitParserFreeFunctionPublication`; the unused nontransactional request
  builder and direct `publishParserFreeFunction` adapter are deleted. The
  abandoned `SymbolTableInsertUndo` journal, `insertWithUndo`, and
  `rollbackInsert` APIs are also deleted; reachable parser publication uses
  plain `SymbolTable::insert`, whose successful insertion remains authoritative
  when shadow publication rejects.
- `gChunkedAnyStorage` is a guarded `LegacyAstChunkedAnyVector`. `emplace_back`
  and `ASTNode::emplace_node` reject types outside the compile-time legacy
  allow-list in `LegacyChunkedAnyAllowList.h`; new semantic records must use
  `FrontendContext` typed arenas.
- Persistent scope **metadata** (`ScopeId`, parent, `ScopeType`, depth,
  namespace handle) is stored in a FrontendContext-owned
  `ChunkedVector<ScopeRecord, 256>` (`sizeof(ScopeRecord) == 16`). `Parser::parse()`
  reconstructs `gSymbolTable` then `bindPersistentScopePublication` opts that
  table in. `AstToIr::symbol_table` does not opt in. Lookup, symbol maps,
  using-directives, and aliases remain on `SymbolTable::scopes_`. Publication
  without an active `FrontendContext` is `InternalError`. `publishScopeState` is
  deleted; `scopeCount()` / `currentScopeId()` on `FrontendContext` are
  arena-backed. The duplicate `Scope::scope_id` field is deleted.
  `SymbolTable::currentScopeId()` derives identity from the append-only slot; bounds-checked ID access no longer
  compares a second stored ID. `ScopeRecord::id` remains context-owned. Scope
  IDs are translation-unit-local slot identities, not context/generation tags.
  `Scope` now holds symbol maps, using-directives, aliases, and namespace
  spelling only; parent, depth, type, and namespace-handle metadata live in
  `SymbolTable::scope_metadata_` for unbound tables and in `ScopeRecord` when
  persistent publication is enabled. Lookup, ADL namespace-chain walks,
  using-directive/declaration collection, namespace-alias resolution, and
  scope-limit depth read `ScopeMetadataView` from `FrontendContext`
  `ScopeRecord` arenas when persistent publication is enabled; unbound
  `SymbolTable` instances read the parallel `scope_metadata_` sidecar.
  `SymbolTable::insert`, `enter_scope`, `enter_namespace`, `exit_scope`,
  `replace_variable`, using-declaration materialization, and
  `DeclarationBuilder::resolvePublicationTarget` read scope metadata through
  `ScopeMetadataView` in both modes. Each published `SymbolTable` binds to
  the `FrontendContext` active at `enablePersistentScopePublication()`; lookup,
  publication, cursor updates, and reset read that bound arena rather than
  `FrontendContext::active()`.
  `SymbolTable::insert` stamps `DeclarationNode::lexical_scope_id` (and
  function, template-function, variable, template-variable, bare declaration,
  struct, enum, typedef, and class-template wrapper nodes) at the shared insert
  choke point.
- `MonotonicScratchArena` requires an explicit diagnostic engine and byte
  budget. `FrontendContext` owns the engine for scratch-limit diagnostics and
  supplies a 64 MiB budget; legacy diagnostics remain in `CompileContext`.
  Allocation enforces `currentBytes() + discardedBytes() <= byteLimit()` and
  `reservedBytes() <= byteLimit()` before publishing state. Rollback transfers
  live bytes into discarded work without replenishing the budget or throwing
  for exhaustion. Padding is charged using actual addresses through
  `std::align`; new blocks are capped at the remaining reservation before
  checking whether the actual alignment fits, so worst-case padding does not
  reject exact-budget typed allocations. Block growth and size arithmetic are
  checked. Exhaustion is
  structured fatal diagnostic `ScratchAllocationLimit` (#3001).
  The limit covers payload, block reservation, and cumulative allocation work,
  not vector/destructor metadata. The 64 MiB value is policy headroom, not a
  measured production workload: production parser probes do not yet use this
  arena. Diagnostic rendering must occur while its owning engine is alive.
  Measured x64 sizes: arena 80 -> 96 bytes; `DiagnosticEngine` 144 bytes;
  `FrontendContext` 1456 bytes plus 48 bytes for syntax-family count storage.
- `printArenaTelemetry` fills scratch, syntax, semantic, and IR
  `AllocationDomain` stats. Syntax used/reserved bytes are forwarded from
  `gChunkedAnyStorage`; semantic used/reserved bytes come from DeclarationBuilder
  declaration and entity arenas (`sizeof` 32 each). Semantic peak used/reserved
  bytes are recorded at arena allocation, not at telemetry refresh. IR domain
  bytes record `IrToObjConverter` text-section and initialized global-data
  lowering buffers at object write; object-writer section buffers remain outside
  this slice until a dedicated IR arena exists. Syntax objects are also grouped
  into coarse AST families (declaration, statement, expression, type-specifier,
  template, other) via `SyntaxAstTelemetry.h`. Semantic declaration and entity
  records are grouped by `DeclKind` through `DeclarationBuilder`; declarator and
  parameter-list intern registry sizes report under `--perf-stats`. InlineVector
  spills attribute to named families (`overload-resolution`, `template-argument`);
  every explicit `InlineVector<T, N>` in `src/` and doctest fixtures names a
  family (no `unknown` spill attribution from untagged containers). Global spill
  totals remain for compatibility. `ChunkedVector`
  reserved bytes count retained chunk capacity across rollback. String-table
  entries/spelling bytes, InlineVector spills, scope count/current `ScopeId`,
  scope-arena used/reserved bytes, declaration/entity counts, and per-arena
  used/reserved bytes are reported under `--perf-stats`. Sampled compiler tests
  peaked at 114 persistent scopes; chunk size 256 is explicit headroom.
- Migration choke-point counters and the inline `find('$')` inventory share one
  fixed corpus baseline (`tests/migration_counters/`). Coordination agents run
  on Linux or Windows, so each platform has a native runner:
  `tests/run_migration_counters.sh` / `tests/run_migration_dollar_inventory.sh`
  on Linux and the `.ps1` equivalents on Windows. Shared parsing lives in
  `tests/runner/runner_common.sh` and `tests/runner/RunnerCommon.ps1`. After
  compiler-source changes, agent validation must use the runner for the host OS.
  Ubuntu and Windows CI invoke the matching native scripts so either merge path
  enforces the same baselines.

## Pull request boundary status (1–32)

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
| 10 | `prepareFunctionPublication` preflight, `PublicationTransaction` mark/rollback, parser shadow commit helper |
| 11 | Initial `SymbolTableInsertUndo` slice for wired free-function inserts (superseded and deleted at boundary 28) |
| 12 | `DeclarationNode::lexical_scope_id` stamped at `SymbolTable` insert/replace/insertGlobal choke point (function, template-function, variable, template-variable, and bare declaration nodes) |
| 13 | Syntax and semantic allocation-domain used/reserved bytes reported through `FrontendContext` from `gChunkedAnyStorage` and DeclarationBuilder arenas |
| 14 | FrontendContext-owned `ScopeRecord` arena; opt-in dual-write from `SymbolTable` enter/exit/clear; `publishScopeState` deleted |
| 15 | Delete duplicate `Scope::scope_id` storage; route identity reads through slot-based `currentScopeId()`; compile-time field guard and mutation-validated 4096-level scope/sibling regression |
| 16 | Explicit scratch allocation budget, context-owned scratch-limit diagnostics, checked address alignment and block publication; delete the unbounded allocation path |
| 17 | Route SymbolTable lookup scope-chain metadata through `ScopeMetadataView` / `ScopeRecord` when persistent publication is enabled; preserve legacy sidecar reads for unbound tables; mutation-validated poison test |
| 18 | Route insert, enter/exit, replace, using-decl materialization, and publication target resolution through `ScopeMetadataView` when persistent publication is enabled |
| 19 | Delete `Scope::{parent_scope_id, scope_type, depth, namespace_handle}`; unbound tables use `scope_metadata_` sidecar; align stale `ScopeId` publication contract in tests; harden publication binding lifetime and rebind sync |
| 20 | Stamp `lexical_scope_id` on struct, enum, typedef, and class-template wrapper nodes at `SymbolTable` insert/replace/insertGlobal |
| 21 | Wire IR allocation-domain bytes from codegen lowering buffers; report coarse syntax AST family counts through `FrontendContext` |
| 22 | Enforce migration counter and dollar-inventory baselines on Ubuntu CI via native bash runners; keep PowerShell runners on Windows CI |
| 23 | Report semantic `DeclKind` breakdown and DeclarationBuilder intern registry sizes through `FrontendContext` arena telemetry |
| 24 | Route constexpr and substitution call-argument template instantiation through `TemplateEngine::tryInstantiateTemplateFromCallArguments`; delete direct external `Parser` calls |
| 25 | Route sema binary operator-template instantiation through `TemplateEngine::tryInstantiateOperatorTemplateForBinary`; delete direct external `Parser` call |
| 26 | Attribute InlineVector spills to named families; tag overload-resolution and template-argument hot vectors; report per-family counts in arena and migration telemetry |
| 27 | Route AstToIr constructor-template materialization through `TemplateEngine::materializeMatchingConstructorTemplate`; delete last external direct `Parser` constructor-template call |
| 28 | Mutation-validate parser shadow merging and retained SymbolTable authority; delete the unused nontransactional parser-publication adapter and abandoned `SymbolTableInsertUndo` control path |
| 29 | Route `ExpressionSubstitutor` dependent member-function template instantiation through `TemplateEngine::tryInstantiateMemberFunctionTemplateCall`; delete last external direct `Parser` member-template call bypass |
| 30 | Tag all `InlineVector` containers in `OverloadResolution.h` with `InlineVectorSpillFamily::OverloadResolution`; overload-resolution spill telemetry no longer attributes to `unknown` |
| 31 | Tag `lookupMemberFunctionTemplateCandidatesForInstantiation` spill vectors with `OverloadResolution`; attach `TemplateEngine` in `Templates:InheritedStaticStructMemberUsesInstantiatedOwner` doctest |
| 32 | Tag every explicit `InlineVector` in `src/` and doctest fixtures with `overload-resolution` or `template-argument` spill families |

Pull request boundaries are not the same as architecture boundaries 0–11.
Architecture boundary 0 tracking slices are substantially closed; architecture
boundary 1 is started, not finished.

## Criteria completion

- Explicit exit criteria total: 78 (boundaries 0 through 11)
- Completed: 9/78 (11.5%). Declaration/lookup
  `ScopeId` stamping is a persistent-scope deliverable, not an explicit exit
  criterion.
  - Boundary 0 "diagnostics emitted outside the engine have a baseline and a
    named removal target in architecture boundary 11"
  - Boundary 0 "structured diagnostics can be asserted by tests"
  - Boundary 0 "choke-point counters and the remaining static inventories are
    visible in CI on a fixed corpus"
  - Boundary 1 "IDs cannot be constructed from pointers"
  - Boundary 1 "discarded scratch bytes are measured and bounded": explicit
    allocation-work and reservation limits, eight budget probes, five rejected
    source-copy mutations (work, padding, reservation, exact-budget preflight,
    alignment-exhaustion classification), and a
    4096-failed-probe test passing with the normal 1 MiB Windows stack.
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
    facade": all non-parser modules (`ConstExpr`, `ExpressionSubstitutor`,
    `SemanticAnalysis`, `AstToIr`) route template instantiation through
    `TemplateEngine`; parser-internal paths remain direct until boundary 6/8A
  - Boundary 1 "a scratch transaction can allocate declarations and types, fail,
    and leave every committed registry unchanged": scratch proven in doctest;
    `PublicationTransaction` covers DeclarationBuilder entity/declaration
    arenas on the parser shadow path; publication reject leaves the
    `SymbolTable` insertion in place because lookup remains authoritative;
    parser tentative parsing and non-wired insert families remain open
  - Boundary 1 "arena bytes, record counts, string-table bytes, and selected
    InlineVector spill counts are reported through FrontendContext": all four
    allocation domains now report used/reserved bytes (IR covers lowering
    buffers only); coarse syntax AST family counts, declaration/entity record
    totals, semantic `DeclKind` breakdown, declarator/parameter-list intern
    registry sizes, and per-family InlineVector spill breakdown report for all
    tagged compiler `InlineVector` containers (`overload-resolution` and
    `template-argument`); full IR arena ownership remains open
  - Boundary 1 persistent-scope ownership deliverable (not an explicit exit
    criterion): compact `ScopeRecord` metadata is context-owned; duplicate
    `Scope::scope_id` and legacy metadata fields on `Scope` are deleted.
    Lookup and insert/enter/exit scope-chain metadata read `ScopeRecord` when
    publication is enabled, or `scope_metadata_` on unbound tables, while symbol
    maps still live on `SymbolTable::scopes_`. The 4096-level enter/exit/sibling
    probe passes with a 1 MiB stack. This scope path is iterative; broader parser/template stack
    bounds remain open.
  - Boundary 1 remaining exit criteria (full merge rules beyond the initial
    free-function set, full template-facade coverage): follow-on boundary-1 work.
    Parser-wired coverage now proves reopened namespace blocks retain distinct
    lexical `ScopeId`s while sharing one `OwnerId` / `EntityId`, and preserves
    `inline` plus definition state through the redeclaration chain.

Boundary-32 validation: `build_flashcpp.bat`; migration counter baselines
unchanged; `comparison_operators_ret1.cpp`,
`concept_comprehensive_ret15.cpp`, and
`test_template_recursive_static_constexpr_member_ret0.cpp`; zero untagged
`InlineVector<` instantiations under `src/`; and `git diff --check` pass.

## Effort estimate

- Implementation effort completed overall: 32-35%, confidence medium

## Remaining work

Replaces the previous remaining-work section entirely on every update.

Next blocker:

- Continue architecture boundary 1: expand shadow wire or merge coverage
  (default arguments, exception specifications, friends, templates) only
  after canonical `TypeId` exists; keep `SymbolTable::insert` as function
  merge authority until canonical function/type identity (boundary 3A) replaces
  the `matches_signature` bridge.

Named follow-ups carried forward:

- Before architecture boundary 10A, approve a parser-family routing table for
  the single translation-unit parse entry point.
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
- Scratch `allocateObject` can finish construction before destructor-vector
  registration throws `bad_alloc`, leaving that object's destructor unregistered.
  Budget rejection now precedes construction, but allocator-failure exception
  safety is still required before production nontrivial scratch probes adopt
  this API. Owner: scratch object lifetime registration.
- Declaration-parse errors can be masked by the top-level expression-statement
  fallback; details and the conversion rule for affected sites live in
  docs/KNOWN_ISSUES.md. Owner: parser declaration dispatch.
- The parser `TypeId` bridge interns via `matches_signature`, which ignores
  nested `FunctionSignature` data (e.g. `void f(void (*)(int))` vs
  `void f(void (*)(double))` can share a builder signature). Owner: boundary 3A
  canonical types; do not delete `SymbolTable` merge on this interner.
