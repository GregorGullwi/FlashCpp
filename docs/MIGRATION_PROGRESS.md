# Front-end migration progress

Current state for the authoritative
[front-end rearchitecture plan](2026-08-24-front-end-rearchitecture-plan.md).
Keep completed work concise; earlier implementation and validation details are
recoverable from git history. Replace stale state rather than appending history.

Last updated: 2026-09-04 after canonical type foundation (feature branch)

## Current boundary and handoff

Architecture boundary 3A's first slice is on `codex/canonical-type-nodes`,
pending review (pull request boundary 38). Gate 0 is closed. Architecture
boundary 1 remains incomplete; further shadow/merge coverage waits for
canonical function/signature identity. Pull request numbers are distinct from
architecture boundaries 0–11.

- `FrontendContext` owns a pinned, single-mutex `CanonicalTypeTable` with
  immutable recursive nodes for C++20 fundamental types, cv qualification,
  pointers, and references. Qualification unions cv bits; references collapse.
  Construction accepts no spelling, parser/member-context, or legacy flat-type
  inputs. `TypeId` is context-local, never a cross-context hash or ABI name.
- `DeclarationBuilder` uses distinct `TelemetryTypeId` keys. Its
  `matches_signature` bridge cannot produce or consume canonical `TypeId` and
  must not diagnose or erase lookup state. `SymbolTable` retains lookup and
  merge authority. No production declarator path has migrated to the table.
- Remaining 3A work: arrays, functions, member pointers, records/enums,
  templates, dependent types, speculative canonical publication, the declarator
  adapter, and deletion of the flat semantic representation. Capture a
  production structural-request trace before reconsidering the single mutex.
  Canonical function signatures are the blocker for declaration-merge migration.
- Stop here for review of this slice. Do not begin another type family, 3B, or the
  parallel-frontend experiment without operator direction and the plan's gates.

The shallow canonical probe measures 31 nodes: the arena uses 32-element chunks
(256 node bytes). Nodes are 8 bytes; the table is 336 bytes on Windows clang-cl.
Node arena accounting excludes the interner hash index's heap storage. The
65,536-level pointer probe passes with the normal 1 MiB Windows stack. Largest
new table-method frame: `reference`, 216 bytes (unoptimized clang-cl
`-fstack-usage`; no predecessor frame). Table methods do not recurse with type
depth; legacy template recursion remains unbounded.

## Established foundation

Completed delivery details for pull request boundaries 1–37 are condensed here.
These capabilities do not imply completion of every architectural exit criterion.

| Area | Established behavior / retained boundary |
|------|------------------------------------------|
| Diagnostics and guards | Diagnostic engine, structured filename contracts, legacy `_fail` conversion, architectural probes, fixed-corpus migration counters, and CI guards. |
| Template facade | Non-parser callers in `ConstExpr`, `ExpressionSubstitutor`, `SemanticAnalysis`, and `AstToIr` use `TemplateEngine`; parser-internal bypasses remain until boundaries 6/8A. |
| Declarations | Initial free-function shadow merging and persistent declaration/entity arenas; namespace `extern` and array-bound redeclarations still merge through `SymbolTable`. |
| Ownership and telemetry | Strong IDs, persistent scope metadata, budgeted scratch, guarded legacy AST allocation, four allocation-domain statistics, and mandatory named InlineVector spill families. |
| Multi-TU / Gate 0 | COFF vague-linkage COMDATs, ELF RTTI/vtable/global-pointer RELRO, working ELF unwind records, template `typeid(T)`, and corrected direct/indirect/virtual reference-argument lowering. |

Preserve these ownership contracts during subsequent migration:

- `DeclarationBuilder` owns typed `DeclId`/`EntityId` arenas (32-byte records).
  Entity identity uses `OwnerId` from namespace registry identity; lexical
  location uses `ScopeId`. Namespace/global C++ non-template free functions
  publish after `SymbolTable::insert` through
  `commitParserFreeFunctionPublication`, `prepareFunctionPublication`, and
  `PublicationTransaction`. Rejected shadow publication leaves lookup intact.
  The nontransactional adapter and `SymbolTableInsertUndo` APIs are deleted.
- `FrontendContext` owns `ChunkedVector<ScopeRecord, 256>` (16-byte records;
  sampled peak 114 scopes). `Parser::parse()` reconstructs and binds
  `gSymbolTable`; `AstToIr::symbol_table` remains unbound. Bound tables use their
  owning context, not whichever context is currently active. Publication needs
  an active context or throws `InternalError`. IDs are TU-local slots, not
  generation tags. `ScopeMetadataView` serves both context records and the
  `scope_metadata_` sidecar for unbound tables. Symbol maps, using-directives,
  and aliases remain on `SymbolTable`. Duplicate scope metadata is deleted;
  shared insert paths stamp lexical IDs on declaration nodes and wrappers.
- New semantic records cannot enter `gChunkedAnyStorage` or
  `ASTNode::emplace_node`: `LegacyChunkedAnyAllowList.h` enforces typed arenas.
- Scratch has a context-owned diagnostic engine and explicit 64 MiB budget.
  Before publication, enforce `currentBytes() + discardedBytes() <= byteLimit()`
  and `reservedBytes() <= byteLimit()`, including actual alignment padding and
  checked size arithmetic. Rollback never replenishes the allocation-work budget.
  Exhaustion is `ScratchAllocationLimit` (#3001). Metadata is excluded; 64 MiB
  is policy headroom, not a measured production workload. Production parser
  probes do not yet use this arena. Render diagnostics before engine destruction.
- `--perf-stats` covers scratch, legacy syntax, declaration/entity/canonical
  node arenas, and IR lowering buffers. Full IR ownership and object-writer
  section-buffer accounting remain open. Counts include AST families, `DeclKind`,
  intern registries, strings, scopes, and named InlineVector spills
  (`overload-resolution`, `template-argument`). Declaration/entity peaks are
  recorded at allocation; retained chunk capacity counts across rollback.
- Completed object-writer invariants: C++ vague linkage governs COMDAT/weak
  emission; unique out-of-line functions remain strong. ELF read-only objects
  with pointer relocations use `.data.rel.ro`. Unwind relocations follow record
  order, omit per-object terminators, and keep FDE references local to each
  object's text. See architecture boundary 2 in the plan for the ABI decision.

## Validation and compatibility baselines

Latest foundation validation: MSVC sharded build; 2,972 single-file tests,
12 multi-TU cases, and 264 negative contracts pass. Native canonical tests and
five source-copy mutations pass; mutation rejection requires exit 1, not a
compile failure or crash. Windows and Ubuntu CI run these guards. Context/
declaration doctests: 91 pass; two failures reproduce unchanged on clean
`36d1b33b` (90 passing baseline cases), detailed under Active findings.

Gate 0 evidence remains the warning-free 12-case Windows and ELF PIE/no-PIE
multi-TU corpus plus `tests/runner/run_elf_eh_frame_tests.sh` in both link orders
and PIE modes. Persistent-scope and failed-scratch probes each cover 4,096 levels/
iterations with a 1 MiB stack. The variable-merge probe covers 8,192 redeclarations;
its largest reviewed parser frame was 31,848 bytes versus baseline 31,864
(Clang 18, `-O0`). These bounded paths do not close broader template stack work.

All 64 fixed-corpus entries remain unchanged. Aggregate values:

| Counter | Baseline / current |
|---------|--------------------|
| `ast_to_ir_semantic` | 56 / 56 |
| `codegen_to_parser` | 0 / 0 |
| `declaration_builder_publish` | 14 / 14 |
| `dollar_identity` | 0 / 0 |
| `outside_engine` | 0 / 0 |
| `post_parse_typing` | 0 / 0 |
| `template_old_engine` | 59 / 59 |
| `token_replay` | 382 / 382 |
| Static dollar inventory | 17 / 17 |

Baselines live in `tests/migration_counters/`. After compiler changes, rebuild,
then use the host-native `run_migration_counters` and
`run_migration_dollar_inventory` scripts (`.ps1` or `.sh`); both platforms enforce
them in CI. New static/API guards reject canonical/telemetry ID interchange.
The telemetry type bridge's removal boundary is 3A; outside-engine diagnostics
retain their boundary-11 removal target. Other removal assignments remain in
the authoritative plan. Telemetry gates in `MigrationTelemetryConfig.h` default
on; shipping configuration remains a follow-up.

## Criteria completion

Explicit exit criteria: **9/78 complete (11.5%)**. The nine completed criteria are:

| Boundary | Completed criteria |
|----------|--------------------|
| 0 (3) | Outside-engine diagnostics have a baseline and boundary-11 removal target; structured diagnostics are test-assertable; fixed-corpus counters/static inventories are visible in CI. |
| 1 (6) | IDs cannot be constructed from pointers; discarded scratch bytes are bounded/measured; leaving scope preserves lookup information; initial declaration-merge regressions pass; the legacy allocation guard rejects non-legacy semantic nodes; no new semantic object enters `ChunkedAnyVector`. |

Advanced, not completed:

- **3A:** no parser/member-stack dependency, parse-order independence, and
  string-insertion-order independence are proved for builtin/cv/pointer/reference
  nodes only. Remaining families and production migration keep all three open.
- **0:** complete mutation-validated coverage or tracked expected failures for
  every architectural defect remains open.
- **1:** full template-facade coverage, full merge rules, transactional parser
  probes that leave all committed registries unchanged, and complete arena
  telemetry/ownership remain open. `PublicationTransaction` covers builder
  declaration/entity arenas, not every parser publication family. Scratch
  rollback is proven in doctests, not integrated across production probes.
- Persistent-scope ownership and lexical-ID stamping are deliverables, not
  additional explicit criteria. Initial shadow tests cover reopened namespace
  lexical scopes sharing an owner/entity, plus inline and definition state.

Implementation effort completed is **not yet estimable**; confidence in a
numeric estimate is low. Remaining canonical families, adapters, and legacy
removals need an effort decomposition. Counts of planning or telemetry changes
must not increase an implementation percentage.

## Remaining work

- Finish 3A's canonical families and adapters before expanding boundary-1 shadow
  coverage (default arguments, exception specifications, friends, templates) or
  removing `SymbolTable` merge / `matches_signature` authority.
- Before boundary 10A, approve a parser-family routing table for the single
  translation-unit parse entry point.
- Boundary 11 must resolve raw pre-ICE `std::cerr` dumps in
  `IrGenerator_MemberAccess.cpp` that bypass both diagnostics and the counter.
- Masked declaration-parse errors must use shared declaration dispatch, or have
  their test deleted, before assigning structured diagnostic IDs; see
  [known issues](KNOWN_ISSUES.md).
- Blanket member-function `noexcept` stays deferred until boundaries 5–8 narrow
  exception paths to invariants.

## Active findings

- Two FrontendContext doctests have identical failures on clean `36d1b33b` and
  this branch (two failures/five assertions; neither test disabled):
  `SymbolTable enablePersistentScopePublication requires an active FrontendContext`
  expects absence despite the suite's static context; `FrontendContext syntax
  AST family counts classify legacy bridge objects` misclassifies
  `TemplateEnvironmentSnapshotNode` and `BlockNode` with clang-cl.
  Owner: unit fixture lifecycle / legacy AST telemetry classification.
- `Parser_Templates_Params.cpp:2505` stores a dangling `owner_name` view from
  `QualifiedIdentifierNode::full_name()`'s temporary string. Clean-base and
  branch clang-cl builds both warn. Owner: legacy template-argument lifetime.
- MSBuild's unity-test ClangCL configuration crashes against VS18 STL headers
  (LLVM 20.1 / STL 14.51 mismatch). Use the direct LLVM clang-cl driver for unit
  tests until toolchains align. Owner: `tests/FlashCppTest` toolchain setup.
- `SemanticAnalysis:*QueryTracksAnalysisState` fails on clean `main`; suspected
  shared-static cause is recorded in [known issues](KNOWN_ISSUES.md).
  Owner: sema query lifecycle.
- Scratch `allocateObject` can construct an object before destructor-vector
  registration throws `bad_alloc`, leaving its destructor unregistered. Fix
  allocator-failure exception safety before production nontrivial scratch probes.
  Owner: scratch object lifetime registration.
- Top-level expression fallback can mask declaration-parse errors; see
  [known issues](KNOWN_ISSUES.md). Owner: parser declaration dispatch.
- The `TelemetryTypeId` bridge ignores nested `FunctionSignature` data through
  `matches_signature`: `void f(void (*)(int))` and `void f(void (*)(double))` can
  share a builder signature. Owner: 3A. Do not delete `SymbolTable` merge on this
  interner.
