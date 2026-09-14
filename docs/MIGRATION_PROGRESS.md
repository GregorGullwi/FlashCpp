# Front-end migration progress

Current state for the authoritative
[front-end rearchitecture plan](2026-08-24-front-end-rearchitecture-plan.md).
Keep completed work concise; earlier implementation and validation details are
recoverable from git history. Replace stale state rather than appending history.

Last updated: 2026-09-14 after member class-template qualified owner-chain
registry keys, including nested-namespace and partial-namespace spellings, on
`boundary-3a-nested-class-entity-early`

## Current boundary and handoff

Architecture boundary 3A's member class-template qualified owner-chain
registration is on `boundary-3a-nested-class-entity-early` for review (after
parse-time nested class EntityId publication under class-owned OwnerIds,
direct primary member class-template and member function-template stamping
under published namespace/global class templates, direct member
function-template stamping, free function-template body replay stamping, free
function-template declared type-parameter stamping, signature-aware free
function TemplateDeclId publication, and member class-template
TemplateDeclId).
`reparse_template_function_body` now receives the instantiation context's
published `TemplateDeclId` explicitly and keeps it in a scoped
`active_template_decl_id_` window while parsing the body. Replayed local type
parameter specifiers therefore use the existing type-parameter stamping path;
the scope restores any enclosing context, and an empty ID fail-closed clears it
for the deferred member-function-template families. Because a function name
parses after its return type, published namespace/global free function templates
also stamp declared type parameters retroactively: return-type and parameter
`TypeSpecifierNode`s carrying `template_parameter_identity` bind to the
published `TemplateDeclId` plus the matching Type-kind parameter index, and
the canonical adapter imports those specifiers as `TemplateParameter` nodes.
Replayed free function-template bodies now also stamp dependent plain-member
chains and type-only member template-ids rooted in their published Type-kind
parameters, including `typename T::template Rebind<int>::type`. Member function
templates nested directly in published non-template namespace/global classes
now enter a `TemplateDepthGuard` while their declaration syntax parses, so their
Type-kind parameter specifiers retain the existing identity key. They publish a
signature-aware `TemplateDeclId` under the enclosing class-owned `OwnerId` plus
simple member name, merge structural redeclarations, and distinguish overloads;
the existing retroactive stamp imports their return and parameter specifiers as
`TemplateParameter` nodes. Their replay window uses the same published-function
activation as free templates. Direct member function templates inside a
published namespace/global primary class template use its template-owned
`OwnerId`, merge matching declarations with definitions, distinguish overloads,
retroactively stamp their declared Type-kind parameters, and activate their
published child ID during replay. Nested member templates and member function
templates without either a published enclosing `EntityId` or this direct
class-template owner still fail closed — except direct members of nested
classes whose EntityId now publishes at parse time (see the nested-class
paragraph below). Signature-aware free
function TemplateDeclId publication is keyed by OwnerId +
name + structural signature index: matching shapes merge (including
forward→definition replace preserving an earlier stamp), distinct overloads
get distinct ids, and type-parameter stamping via `active_template_decl_id_`
during class-template body parse stays the class-template path only. Primary
member class templates nested directly in published
non-template namespace/global classes publish `TemplateDeclId` under class-owned
`OwnerId` (enclosing EntityId) plus simple member name; enclosing EntityId is
published as a non-definition before body parse so member templates can stamp
during the body, then merged as a definition at the complete-definition epoch.
Direct primary member class templates inside a published namespace/global
primary class template publish under a disjoint template-owned `OwnerId` derived
from that enclosing `TemplateDeclId`; forward declarations merge with
definitions, and their Type-kind parameter specifiers stamp with the child
primary's ID. Direct member function templates in those class-template bodies
use the same template-owned owner with a structural signature key; their
forward declarations merge with definitions, overloads remain distinct, and
their declared Type-kind parameter specifiers stamp with the child function ID.
Nested classes of published namespace/global non-template classes now publish
their EntityId at parse time: `tryPublishNestedClassIdentity` resolves the
immediate enclosing class from the struct-parsing context stack (back() is the
current class, the owner candidate sits at size-2; `enclosing_class()` is not
set until after the nested parse returns, and class bodies do not enter a Class
ScopeType) and publishes the nested class as a non-definition under the
enclosing class-owned `OwnerId` before the nested body parse, merging the
definition flag at the nested complete-definition epoch. Nested forward
declarations merge into the later definition's EntityId, and same-spelling
nested classes no longer publish at namespace level, so
`Outer::Inner` never conflates with a namespace `Inner` entity. Direct member
class templates and member function templates inside those nested classes then
publish `TemplateDeclId`s during the nested body and retroactively stamp their
declared Type-kind parameters through the existing publication helpers; the
lazy enclosing-epoch path stays as fallback for nested classes whose enclosing
lacked an EntityId at parse time (template-nested, local, and anonymous forms
still fail closed there). Member class templates additionally register
qualified owner-chain registry keys: type-ids spelled through nested enclosing
classes (`Outer::Inner::Box<int>`) or a namespace prefix
(`ns::Outer::Box<int>`) now instantiate the same declaration the simple
member spelling registers, instead of failing with "No primary class template
found" for the missing chain key. The alias key set is built by
`Parser::buildMemberClassTemplateAliasKeys` (bare enclosing chain,
namespace-qualified chain, and partial namespace chains through using-directive
visibility) and registered through `TemplateRegistry::registerTemplateAliases`,
a shared dedup choke point; nested-namespace spellings
(`ns::inner::Outer::Inner::Box<int>`, `inner::Outer::Inner::Box<int>`, and the
bare in-namespace form) resolve to the same declaration. Dependent NTTP Spec
stamping,
concrete and active-dependent primary-class template-template Spec arguments,
explicit dependent NTTP Spec arguments,
ExpressionSubstitutor tip-resolve restamp wire, production named type-member
schema publication, opaque tip-resolve substrate, production dependent_name_type
restamp, opaque `CanonicalTypeTable::substitute`, CurrentInstantiation /
UnknownSpecialization Spec-rooted stamping, Spec-rooted member template-ids,
plain DependentInstantiation members, opaque Spec-as-qualifier identity,
type-only member template-id stamping on type parameters, DependentTemplateMember
identity, plain dependent-member publication, opaque DependentName identity,
production type-only template-id stamping, type-only TemplateSpecialization
TypeIds, primary class-template TemplateDeclId publication, opaque
TemplateParameter TypeId import, dependent-`noexcept` ExprId Functions,
unstructured signatures, calling-convention / dll-linkage callables, record
member/base field schemas, complete-object Record/Enum layout, opaque Record/Enum
import, member-pointer EntityId binding, and earlier families are on `main`.
Gate 0 is closed. Architecture boundary 1 remains incomplete; remaining
dependent-name families and richer specialization arguments still block expanding
shadow/merge coverage. The legacy materialization path now defers an unbound
template-template argument instead of eagerly instantiating its parameter
spelling, then rebinds that stored argument by its owning template parameter
during concrete alias materialization. This fixes forwarded aliases such as
`Forward<Box>::result::result::value`.

- `FrontendContext` owns a pinned, single-mutex `CanonicalTypeTable` for C++20
  fundamental types, cv qualification, pointers, references, arrays of known or
  unknown bound, free-function / cv-ref-qualified function types (including
  calling convention, dllimport/dllexport, plain noexcept, and dependent
  `noexcept(expr)` via context-local `ExprId`), opaque `Record(EntityId)` and
  `Enum(EntityId)` nodes, opaque `TemplateParameter(TemplateDeclId, index)`
  nodes, `TemplateSpecialization(TemplateDeclId, ordered TypeId / ExprId /
  primary-class TemplateDeclId / active template-template parameter-owner args)`
  nodes (with internal argument links),
  opaque `DependentName` nodes (qualifier
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
  versus `T::second`; `T::Foo<int>` versus `T::Foo<double>`;
  `Primary<Args>::member` versus a different primary or argument list), never
  numeric `StringHandle` values or premature lookup. Dependent-name-family
  qualifiers may be a published type parameter, a type-only
  `TemplateSpecialization`, or a prior dependent-name-family node. `FrontendContext`
  also owns a `DependentExpressionTable` that interns dependent unevaluated
  expressions to `ExprId` using structural identity (not `StringHandle`), and a
  `TemplateDeclTable` that publishes and looks up primary class-template and
  free and eligible direct-member function-template `TemplateDeclId`s keyed by
  `OwnerId` + template name + primary kind (+ signature index for function
  overloads; redeclaration merge;
  spelling is a lookup key only). OwnerId may be namespace-mapped, class-owned
  (`ownerIdFromClassEntity`) for member class primaries under published
  non-template enclosing classes, or template-owned
  (`ownerIdFromTemplateDecl`) for direct member primaries under published class
  templates. Distinct free function-template overloads publish distinct
  signature indices rather than sharing OwnerId+name.
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
  `TemplateParameter` (cv/pointer/array/ref wrappers included). Publishable
  non-template namespace/global classes also receive an EntityId as a
  non-definition before body parse so in-body member primary class templates can
  publish under class-owned OwnerId; the complete-definition epoch merges the
  definition flag. Member primary forward declarations and definitions under
  those enclosing classes stamp `TemplateDeclId` onto Struct /
  TemplateClassDeclarationNode and set `active_template_decl_id_` for the member
  body window (fail-closed clear when the enclosing EntityId is absent). Explicit
  type-only class template-ids for published primaries are stamped during
  `parse_type_specifier` (preferring syntax-node args so nested stamps survive)
  and import as `TemplateSpecialization` when every argument imports Supported.
  Opaque Spec identity also accepts mixed type / NTTP `ExprId` / published
  primary-class `TemplateDeclId` arguments in the canonical table. Production
  stamping interns bool / integral literal and explicit dependent NTTP
  ExpressionNodes as opaque ExprIds on published class-template Specs (including
  Spec-rooted DependentInstantiation / CurrentInstantiation /
  UnknownSpecialization owners). Concrete non-pack namespace/global primary
  class templates used for fixed template-template parameters stamp their
  published `TemplateDeclId`. While a published namespace/global primary class
  template body is parsed, a non-pack dependent template-template argument that
  names its active template parameter stamps that primary's `TemplateDeclId`
  plus parameter index. Explicit concrete type arguments for a final
  namespace/global primary-class type pack stamp as ordered TypeId arguments;
  dependent pack expansions, non-type/template packs, other dependent
  template-template arguments, member function templates or nested member
  templates under unpublished enclosing forms (nested classes before their
  EntityId epoch, local/anonymous), member-template partials, alias expansions,
  `Template<args>::member` results beyond that Spec-rooted path, and
  defaults-without-`<>` stay unstamped; completed instantiations may still
  appear as Record via EntityId.
  Spelling-only bindings (member templates under unpublished enclosing classes,
  NTTP / template-template parameters, and uses before publication) stay
  Unresolved; free and eligible direct-member function templates, including
  direct members of published class templates, publish
  signature-aware TemplateDeclIds and stamp their declared type parameters
  retroactively, including while their published function-template bodies
  replay).
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
  is a no-op for unpublished templates, non-type parameters, and NTTP / pack or
  template-template member arguments. Spec-rooted chains under
  DependentInstantiation, CurrentInstantiation, or UnknownSpecialization
  (`Primary<Args>::member`, nested plain tails, and type-only member
  template-ids when member-arg syntax is captured) are stamped on the live
  template-id-then-`::` placeholder path: the owner Spec is built from the
  published primary's TemplateDeclId plus type-only argument TypeIds, then the
  member chain walks with DependentName / DependentTemplateMember. Legacy
  `owner_kind` remains parse/materialization metadata and is not a TypeId
  discriminant. Missing or non-type member-arg syntax leaves the specifier
  unstamped instead of rebuilding from TypeIndex names. The adapter also imports
  structured callables, flat TypeIndex projections, and dependent-noexcept
  signatures with published `ExprId` as Supported when every component imports.
  Invalid-category TypeIndex projections stay UnmigratedCallable. The production
  adapter fixture remains 25 supported / 0 deferred and emits Record/Enum array
  and function traces. `SymbolTable` retains lookup and merge authority.
- `CanonicalTypeTable::substitute(TypeId, TemplateDeclId, arg TypeIds)`
  structurally replaces matching `TemplateParameter` nodes, rebuilds Spec /
  DependentName / DependentTemplateMember / cv / pointer / array / reference
  wrappers iteratively, preserves opaque Spec NTTP `ExprId` and template-template
  `TemplateDeclId` arguments and active dependent template-template parameter
  owner/index arguments, and
  leaves unresolved member tips as DependentName-family nodes (concrete
  qualifiers allowed only as substitute results; public `dependentName` still
  requires a dependent qualifier kind). Function and member-pointer walks throw.
  Nodes remain 16 bytes.
- `ExpressionSubstitutor::substituteInType` runs the legacy TypeIndex body
  unchanged, then fail-closed overlays `dependent_name_type_` when
  `CanonicalTypeTable::substitute` succeeds with a unique TemplateDeclId
  environment discovered from the stamp graph, type-only Supported imported
  arguments, and a tip that remains DependentName or DependentTemplateMember.
  Pack / NTTP / template-template bindings leave the stamp unchanged. Overlay
  never re-attaches a DependentName tip onto a concrete resolved TypeIndex.
  After substitute, `tryResolveDependentTip` runs: DependentName-family tips are
  restamped; collapsed concrete tips project Builtin/Record/Enum onto legacy
  TypeIndex / category / EntityId (Qualified peel applies tip CV), then clear
  `dependent_name_type_` (adapter forbids non-DependentName-family stamps).
  Pointer/ref/array/function tips Clear without projection. Base-walk tip
  lookup remains deferred.
- EntityId-keyed named type-member schemas store NameBytes identifier content plus
  target `TypeId` (independent of spelling-free layout `CanonicalRecordMember`
  schemas). `tryLookupNamedTypeMember` and `tryResolveDependentTip` collapse plain
  DependentName chains only when every step is `Record` with a published name hit;
  DependentTemplateMember, misses, and non-Record qualifiers leave the tip
  unchanged. No StringHandle identity or SymbolTable. Complete published namespace/global records fail-closed publish Supported nested
  typedef/using RHS TypeIds and nested classes under class-owned OwnerIds
  (`ownerIdFromClassEntity`, tagged so they cannot collide with namespace-mapped
  owners). Nested EntityIds now publish at nested parse time under the enclosing
  class-owned OwnerId (non-definition before nested body parse; definition
  merged at the nested complete-definition epoch; nested forward declarations
  merge into the definition EntityId); the enclosing-epoch path publishes
  nested and enclosing named type-member schemas and remains the fallback when
  parse-time publication was unavailable.
  Local/anonymous/template-nested classes remain omitted. ExpressionSubstitutor
  restamp runs `tryResolveDependentTip` (Set DependentName-family / Clear on
  collapse with Builtin/Record/Enum TypeId→TypeIndex projection). Nodes remain
  16 bytes; `sizeof(CanonicalTypeTable)` is 2,680 bytes on Linux clang++.
- Canonical nodes participate in nested publication and frontend scratch
  transactions. Rollback reuses discarded arena slots; committed IDs remain
  stable. Dependent-expression and template-decl interning are not transactional.
- Remaining 3A work includes nested member-template Spec-rooted dependent
  stamping, complete declarator interleaving, and deletion of the
  flat semantic representation. Stop here for review before starting another
  family, 3B, or the parallel frontend experiment.

The shallow native probe measures 80 nodes. Nodes are 16 bytes; member and base
schema records are 16 bytes; `sizeof(CanonicalTypeTable)` is 2,680 bytes on
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

Latest validation for member class-template qualified owner-chain keys:
sharded rebuild; the
`test_canonical_nested_member_template_qualified_id_ret0` (nested enclosing
chain, member function template in the same nested class),
`test_canonical_namespace_member_template_qualified_id_ret0` (namespace
prefix, unqualified in-namespace use), and
`test_canonical_nested_namespace_member_template_ret0` (fully qualified,
partial namespace through a using-directive, and bare in-namespace spellings)
source regressions compile and return 0 with mixed native widths and struct
arguments; FlashCppTest verifies the legacy prefix key and the namespace
chain key resolve to one declaration with one TemplateDeclId. Stashing the
chain-key registration fails the qualified-id source regressions. The full
Linux suite passes (2,978 single-file cases, 264 negative tests, 12 multi-TU
cases, 0 crash / 0 mismatch). Fixed-corpus
migration counters remain within baseline; `template_old_engine` on
`test_template_recursive_static_constexpr_member_ret0.cpp` measured 58 on
Linux while MSVC still measures 59, so the shared baseline stays 59 and the
Linux value reports Improved.
Adjacent architecture coverage remains the DependentName / Spec-rooted /
substitute / restamp / tip-schema / tip-projection / opaque-NTTP probes.
Fixed-corpus migration counters remain within the prior baselines below.

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
  chains rooted in published type parameters, opaque DependentTemplateMember
  identity with production stamping of type-only member template-ids, opaque
  DependentName / DependentTemplateMember chains rooted in type-only
  TemplateSpecialization qualifiers, production stamping of Spec-rooted chains
  for DependentInstantiation / CurrentInstantiation / UnknownSpecialization
  owners (plain members and type-only member template-ids), and opaque
  structural `CanonicalTypeTable::substitute` for type-parameter environments,
  production fail-closed `dependent_name_type_` restamp through
  ExpressionSubstitutor, opaque named type-member schemas with
  `tryResolveDependentTip`, production fail-closed publish of Supported nested
  typedef/using schemas on complete published records, and ExpressionSubstitutor
  tip-resolve restamp (Set DependentName-family / Clear on collapse), nested
  class EntityId ownership under class-owned OwnerIds, and TypeId→TypeIndex
  projection for collapsed Builtin/Record/Enum tips on restamp Clear, and opaque
  Spec NTTP `ExprId` arguments (with substitute preserving them), production
  stamping of bool / integral literal NTTP Spec args (opaque ExprId intern), and
  concrete published primary-class template-template Spec args (with substitute
  preserving them), explicit concrete final type-pack Spec args with ordered
  links, active dependent template-template Spec args represented by owning
  TemplateDeclId plus parameter index (with substitute preserving them),
  explicit dependent NTTP Spec args represented by opaque ExprIds, primary
  member class-template TemplateDeclId publication under published non-template
  enclosing classes (class-owned OwnerId + early enclosing EntityId before body),
  and signature-aware free function TemplateDeclId publication (kind-tagged
  OwnerId+name+signature index with shape-matched redeclaration merge), and
  retroactive stamping of free function-template declared type parameters
  (return/parameter specifiers bind to the published TemplateDeclId + Type-kind
  parameter index, adapter-imported as TemplateParameter), and scoped published
  TemplateDeclId activation while those free function-template bodies replay,
  including structural plain-member chains and type-only member template-ids
  rooted in their published Type-kind parameters, and direct member function
  templates under published non-template namespace/global classes (class-owned
  OwnerId + signature-aware publication, retroactive Type-kind parameter stamp,
  and published replay activation), and direct primary member class templates
  under published namespace/global class templates (template-owned OwnerId,
  forward-to-definition merge, and child Type-kind parameter stamps), and
  direct member function templates under those class templates (template-owned
  OwnerId + signature-aware publication, redeclaration merge, overload
  separation, retroactive Type-kind parameter stamp, and replay activation), and
  parse-time nested class EntityId publication under enclosing class-owned
  OwnerIds (non-definition before nested body parse, definition merge at the
  nested complete-definition epoch, nested forward-to-definition merge, no
  namespace-level nested publication), and member class-template plus member
  function-template publication under those published nested classes (with
  declared Type-kind parameter stamps), and member class-template qualified
  owner-chain registry keys (nested enclosing chain and namespace prefix, so
  qualified type-ids instantiate the same declaration) are
  landed; nested member-template Spec-rooted dependent stamping, alias,
  unpublished/incomplete nominal, anonymous-union, and
  unpublished-base forms stay deferred. Remaining families and flat-field
  deletion keep all three identity criteria open.
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

- Nested member-template Spec-rooted dependent stamping,
  then richer adapters before expanding boundary-1 shadow coverage (default
  arguments, exception specifications, fields, templates) or removing
  `SymbolTable` merge / `matches_signature` authority.
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
