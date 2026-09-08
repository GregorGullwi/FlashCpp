# Front-end migration progress

Current state for the authoritative
[front-end rearchitecture plan](2026-08-24-front-end-rearchitecture-plan.md).
Keep completed work concise; earlier implementation and validation details are
recoverable from git history. Replace stale state rather than appending history.

Last updated: 2026-09-08 after production stamp of type-only member
template-ids on the live `T::Foo<Args>` parse path (local feature branch)

## Current boundary and handoff

Architecture boundary 3A's dependent template-member slice is on
`codex/boundary-3a-dependent-template-id-members` for local review. Production
plain dependent-member publication, opaque DependentName identity, production
type-only template-id stamping, type-only TemplateSpecialization TypeIds,
primary class-template TemplateDeclId publication, opaque TemplateParameter
TypeId import, dependent-`noexcept` ExprId Functions, unstructured signatures,
calling-convention / dll-linkage callables, record member/base field schemas,
complete-object Record/Enum layout, opaque Record/Enum import, member-pointer
EntityId binding, and earlier families are on `main`. Gate 0 is closed.
Architecture boundary 1 remains incomplete; remaining dependent-name families
and richer specialization arguments still block expanding shadow/merge coverage.

- `FrontendContext` owns a pinned, single-mutex `CanonicalTypeTable` for C++20
  fundamental types, cv qualification, pointers, references, arrays of known or
  unknown bound, free-function / cv-ref-qualified function types (including
  calling convention, dllimport/dllexport, plain noexcept, and dependent
  `noexcept(expr)` via context-local `ExprId`), opaque `Record(EntityId)` and
  `Enum(EntityId)` nodes, opaque `TemplateParameter(TemplateDeclId, index)`
  nodes, type-only `TemplateSpecialization(TemplateDeclId, arg TypeId list)`
  nodes (with `TemplateArg` links), opaque `DependentName` nodes (qualifier
  TypeId plus identifier content in internal `NameBytes` links), opaque
  `DependentTemplateMember` nodes (qualifier TypeId, identifier `NameBytes`, and
  type-only `TemplateArg` links — unresolved member template-ids without a
  published member `TemplateDeclId`), member object/function pointers,
  EntityId-keyed complete-object layout snapshots, and EntityId-keyed record
  member/base field schemas. Immutable 16-byte nodes use context-local `TypeId`;
  parameter lists, specialization arguments, member-pointer owners, and
  dependent-name identifier bytes are recursive links, not a second identity
  space. Construction accepts no spelling handle or legacy flat-type identity;
  unresolved dependent member identifiers are structural content (`T::first`
  versus `T::second`; `T::Foo<int>` versus `T::Foo<double>`), never numeric
  `StringHandle` values or premature lookup. `FrontendContext` also owns a
  `DependentExpressionTable` that interns dependent unevaluated expressions to
  `ExprId` using structural identity (not `StringHandle`), and a
  `TemplateDeclTable` that publishes and looks up primary class-template
  `TemplateDeclId`s keyed by `OwnerId` + template name (redeclaration merge;
  spelling is a lookup key only).
- Published global/namespace structs and enums bind `type_entity` / injected-
  class metadata at declarator intern time so `Struct` and `Enum` declarators
  import as opaque `Record(EntityId)` and `Enum(EntityId)` nodes (with
  cv/ref/pointer wrappers). Complete definitions publish record object size,
  dsize-like cursor, non-virtual size, alignment, member/base counts, union
  flag, enum underlying `TypeId`, enumerator count, and scoped flag through the
  same table. When every data member and direct base imports as Supported, the
  parser also publishes ordered member schemas (`TypeId`, offset, size, access,
  bitfield / `no_unique_address` flags) and base schemas (`EntityId`, offset,
  access, virtual flag). Fixed-bound arrays of those complete published nominal
  types import canonically; aliases, unpublished/incomplete nominal forms,
  unknown nominal bounds, anonymous-union groups, and unpublished-base schemas
  stay deferred. Function types carry `CanonicalCallingConvention` in the
  Function node's builtin byte and dllimport/dllexport as flag bits. Dependent
  `noexcept(expr)` packs an `ExprId` beside the parameter-list link and sets
  `DependentNoexceptFunction`. Namespace/global primary class templates publish
  a `TemplateDeclId` onto `StructDeclarationNode` /
  `TemplateClassDeclarationNode` when the class name is known; while that
  template's body is parsed, type-parameter `TypeSpecifierNode`s are stamped with
  `TemplateDeclId` + parameter index so the adapter imports them as
  `TemplateParameter` (cv/pointer/array/ref wrappers included). Explicit
  type-only class template-ids for published primaries are stamped during
  `parse_type_specifier` (preferring syntax-node args so nested stamps survive)
  and import as `TemplateSpecialization` when every argument imports Supported.
  NTTP / template-template / pack arguments, nested/member templates, alias
  expansions, `Template<args>::member` results, and defaults-without-`<>` stay
  unstamped; completed instantiations may still appear as Record via EntityId.
  Spelling-only bindings (function templates, nested/member templates, NTTP /
  template-template parameters, and uses before publication) stay Unresolved.
  Plain dependent-member chains rooted in a published type parameter
  (`T::first`, `T::Nested::item`) and type-only member template-ids
  (`T::Foo<int>`, `T::template Nested<U>::type`) are stamped during
  `parse_type_specifier` onto `TypeSpecifierNode` as `DependentName` /
  `DependentTemplateMember` TypeIds (cv/pointer/array/ref wrappers import
  through the existing adapter; tip may be either kind). The live template-id
  path is the concatenated qualifier `T::Foo` then `<Args>` then an optional
  plain `::tail`; captured type-arg syntax is required, and NTTP / pack /
  expression arguments leave the specifier unstamped instead of throwing.
  Binding is never recovered from flat TypeIndex names or `TemplateArgInfo`
  spellings. `::template` is a parse disambiguator and is not stored. Stamping
  is a no-op for unpublished templates, non-type parameters,
  `DependentInstantiation` / other qualifier families (`T<Args>::…`), and
  later template-id segments whose type-only syntax was not captured. Other
  qualifier families and substitution stay deferred. The adapter also imports structured
  callables, flat TypeIndex projections, and dependent-noexcept signatures with
  published `ExprId` as Supported when every component imports. Invalid-category
  TypeIndex projections stay UnmigratedCallable. The production adapter fixture
  remains 25 supported / 0 deferred and emits Record/Enum array and function
  traces. `SymbolTable` retains lookup and merge authority.
- Canonical nodes participate in nested publication and frontend scratch
  transactions. Rollback reuses discarded arena slots; committed IDs remain
  stable. Dependent-expression and template-decl interning are not transactional.
- Remaining 3A work includes richer qualifier families (`T<Args>::…`,
  DependentInstantiation owners), substitution, NTTP / template-template / pack
  specialization arguments, function/nested/member TemplateDeclId publication,
  complete declarator interleaving, and deletion of the flat semantic
  representation. Stop here for review before starting another family, 3B, or
  the parallel frontend experiment.

The shallow native probe measures 80 nodes. Nodes are 16 bytes; member and base
schema records are 16 bytes; `sizeof(CanonicalTypeTable)` is 2,048 bytes on
Linux clang++. Its measured 64-element chunks reserve 1,024 node bytes at a
time; hash-index heap storage is excluded. A 65,536-level mixed pointer/array
probe and a 65,536-level dependent-name chain pass under the host stack used by
the native harness. Table construction, cv propagation, function-parameter
linking, specialization-argument linking, member-pointer owner linking,
dependent-name identifier packing, layout/schema publication, and rollback are
iterative.

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
  `PublicationTransaction`. Namespace/global non-template class/struct
  declarations publish through `commitParserClassPublication` /
  `prepareClassPublication` (lookup key signature id 0) and stamp
  `StructDeclarationNode::entity_id`. Rejected shadow publication leaves lookup
  intact. The nontransactional adapter and `SymbolTableInsertUndo` APIs are
  deleted.
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

Latest validation for the dependent template-member slice: native canonical
tests and source-copy mutations pass; `T::Foo<int>` versus `T::Foo<double>` and
`T::Bar<int>` remain distinct TypeIds; plain tails compose as
`DependentName(DependentTemplateMember, "type")`; production stamping runs on
the concatenated `T::Foo` then `<Args>` path (and optional plain tail),
fail-closes on NTTP/expression syntax instead of ICE, and does not rebuild
arguments from TypeIndex names. Other qualifier families stay unstamped;
`::template` is ignored in identity. Production fixture remains
25 supported / 0 deferred. Adjacent `_ret0` coverage includes
`test_canonical_dependent_template_member_ret0.cpp` (OOL function-parameter
path) and prior dependent-name / template-id ret0s. The Windows suite is 2,984
single-file cases, 264 negative tests, and 12 multi-TU cases. Fixed-corpus
migration counters remain within the prior baselines below.

Gate 0 evidence remains the warning-free 12-case Windows and ELF PIE/no-PIE
multi-TU corpus plus `tests/runner/run_elf_eh_frame_tests.sh` in both link orders
and PIE modes. Persistent-scope and failed-scratch probes each cover 4,096 levels/
iterations with a 1 MiB stack. The variable-merge probe covers 8,192 redeclarations;
its largest reviewed parser frame was 31,848 bytes versus baseline 31,864
(Clang 18, `-O0`). These bounded paths do not close broader template stack work.

All 64 fixed-corpus entries remain within baseline. Aggregate values:

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
  string-insertion-order independence are proved for builtin/cv/pointer/reference/
  array/function/record/member-pointer nodes only. Array bounds, unknown bounds,
  dimension order, cv propagation, pointer binding, parameter adjustment, function
  parameter lists, function cv/ref, variadic, noexcept, calling convention,
  dllimport/dllexport, unstructured TypeIndex projections, FunctionPointer
  wrapping, Function-as-parameter decay, Record and Enum EntityId identity, and
  member-pointer owner/pointee distinction are mutation-validated. Class and enum
  EntityId publication, EntityId-backed adapter MOP/MFP import, parse-time
  member-pointer owner binding, opaque Struct→Record and Enum→Enum adapter
  import, complete-object layout snapshots, fixed-bound array import for
  complete published nominal types, EntityId-keyed member/base field schemas,
  cc/dll Function identity, unstructured signature import, dependent-`noexcept`
  ExprId identity, opaque TemplateParameter(TemplateDeclId, index) identity,
  primary class-template TemplateDeclId publication (with type-parameter
  stamping), type-only TemplateSpecialization(TemplateDeclId, arg list)
  identity, production type-only template-id stamping for published primaries,
  opaque DependentName identity with production publication of plain-identifier
  chains rooted in published type parameters, and opaque DependentTemplateMember
  identity with production stamping of type-only member template-ids are landed;
  other qualifier families, substitution, function/nested/member template
  publication, NTTP / template-template / pack specialization arguments, alias,
  unpublished/incomplete nominal, anonymous-union, and unpublished-base forms
  stay deferred. Remaining families and flat-field deletion keep all three
  identity criteria open.
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

- Finish 3A's remaining dependent-name families (other qualifier families such as
  `T<Args>::…` / DependentInstantiation owners, then substitution), richer
  specialization arguments, and adapters before expanding boundary-1 shadow
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
