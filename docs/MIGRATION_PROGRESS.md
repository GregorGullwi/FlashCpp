# Front-end migration progress

Current state for the authoritative
[front-end rearchitecture plan](2026-08-24-front-end-rearchitecture-plan.md).
Keep completed work concise; earlier implementation and validation details are
recoverable from git history. Replace stale state rather than appending history.

Last updated: 2026-09-18 after landing namespace/global alias-template
declaration identity on `boundary-3a-namespace-alias-identity`, the first
bounded split of the dependent-alias family. Namespace and global alias
primaries now publish a `TemplateDeclId` under the namespace-mapped `OwnerId`
and anchor their `TemplateAliasNode`, matching the member alias/variable/class
publication already landed, so alias declaration identity no longer depends on
the registry spelling key. Earlier on `main`: the deferred `MemberObjectPointer`
canonical adapter family (cast/NTTP pointee preservation plus MSVC mangling
recovery), the `parse_type_specifier` `<` gate alias arm, instantiated-owner
member variable identity, instantiated-owner member alias identity,
template-friend member identity, callable substitution, nested callable
parameter builder identity, and out-of-line member-class-template definitions.

## Current boundary and handoff

Architecture boundary 3A resolves class and member-template identity through
published `EntityId` / `TemplateDeclId` rather than registry spelling aliases.
Qualified member class-template ids now resolve by identity end to end:
`Parser::findClassTemplatePatternByIdentityChain` walks the owner chain to a
published class EntityId or a namespace/global primary class template's
published `TemplateDeclId` (the owner spelling is a type-system lookup key
only), calls `TemplateDeclTable::findPrimaryClassTemplate` under the resulting
class-owned or template-owned OwnerId, and returns the pattern node anchored
under that TemplateDeclId by `attachPrimaryClassTemplatePattern` (attached at
wrapper creation for namespace/global primaries and member primaries, with the
definition attach replacing the forward-declaration anchor). An instantiated
primary owner reaches the same template-owned OwnerId through its injected
primary pattern; direct nested primaries remain fail-closed. The legacy
instance-name bridge treats both class-owned and template-owned member
primaries as collision participants, while namespace primaries retain their
spelling-based cache key.
`try_instantiate_class_template` runs that resolution first for names
containing `::` and falls back to the registry lookup fail-closed.
`parse_type_specifier` resolves the same ids through
`Parser::findClassTemplatePatternBySpelling` (identity first, registry
fail-closed fallback) at every spelled class-template lookup — the `<`
disambiguation gate, template-argument pattern selection,
`normalizeDependentNonTypeTemplateArgs`, default-argument fill, the
dependent-placeholder parameter lists, the all-defaulted instantiation path,
and the shared Spec-stamp choke point `collectClassTemplateArgSpecs`, which
now takes the resolved primary pattern via
`Parser::findPrimaryClassTemplateForStamping`. The `TemplateRegistry`
owner-chain alias shim is deleted: member class templates register only the
legacy owner-prefix key (`Inner::Box`) and the simple member name (`Box`), and
partial-namespace-suffix owner spellings (`inner::Outer::Inner`) now come from
the nested type system's type-map aliases, which identity resolution reads.
Nested member class-template bodies now retain published parameter bindings
alongside their active parameter names. The class-template argument parser
recognizes those bindings before legacy projections select a concrete path, and
the shared Spec-stamp collector repairs type-only argument syntax to the
published `(TemplateDeclId, index)` binding before import. Thus
`Root<Inner>::template Rebind<Inner>::type` inside an eligible direct member
class template stamps as a structural `TemplateSpecialization` followed by
`DependentTemplateMember` and `DependentName`; parameter spelling remains only
the scoped lookup key. Unpublished and non-type/template/pack arguments remain
fail-closed.
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
still fail closed there). Primary member alias templates publish identity the
same way: `parse_member_template_alias` publishes a `TemplateDeclId`
(`TemplateDeclTable` `PrimaryKind::Alias`) under the class-owned OwnerId from
the enclosing parse-time EntityId, or a template-owned OwnerId for direct
members of published namespace/global primary class templates, anchoring the
`TemplateAliasNode` under that id. Qualified member alias type-ids resolve
through `Parser::findAliasTemplateBySpelling` (identity chain first via
`findAliasTemplateByIdentityChain`, registry alias lookup fail-closed
fallback) at the `parse_type_specifier` alias lookups and at the shared
materialization choke point `materializeAliasTemplateInstantiation`, so
partial-namespace-suffix spellings such as `m::Gauge::Meter<int>` (class
`Gauge` in namespace `n::m`) that missed the registration-time owner-qualified
registry key now resolve through the owner chain's type-system lookup. Member
variable templates now publish identity the same way:
`parse_member_variable_template` publishes a `TemplateDeclId`
(`TemplateDeclTable` `PrimaryKind::Variable`) under the same OwnerId
derivation, anchoring the `TemplateVariableDeclarationNode`, and qualified
variable-template spellings resolve through
`Parser::findVariableTemplateBySpelling` (identity chain first, registry
variable lookup fail-closed fallback) at the
`try_instantiate_variable_template` choke point and the `parse_type_specifier`
`<` gate's full-name variable arm. The variable-template instance name stem is
collision-disambiguated with `$td<TemplateDeclId>` through
`Parser::getVariableTemplateInstanceKeyStem` (mirroring the class-template
instance bridge), so two same-spelling class-owned variable primaries never
share an instantiation cache entry. The expression-side member-access and
member-template call path now materializes the owning class template through
`findClassTemplatePatternBySpelling` (identity first, registry fail-closed
fallback) in `materializePrimaryTemplateOwnerForLookup` whenever the registry
answer is not a published class template, so same-spelling member class
templates under different owners materialize per-owner `$td` instances and
member calls bind to the right instance's functions. Friend class/struct
declarations naming member class templates resolve through identity first at
the same choke point; the declarator now parses the owner chain component-wise,
so owner template arguments (`friend struct Outer<int>::Box<char>;`) are a
type-system lookup key only while the member primary spelling drops them
(`Outer::Box`) and the member's own arguments remain the granted
specialization. The `template <...> friend struct/class Owner<...>::Member<...>;`
class branch shares that parser and identity resolution instead of the retired
spelling-suffix path, so a member specialization argument is accepted and the
member primary binds by identity; a bare friend class-template name keeps the
all-specializations `FriendKind::TemplateClass` representation.
Instantiated-owner member alias chains
(`Outer<int>::Meter<…>`) now resolve through identity in
`parse_type_specifier`. Instantiated-owner member variable chains
(`Outer<Args>::Meter<…>` expression forms) now resolve through identity after
the member function-call probe. The `parse_type_specifier` `<` gate known-
template test now includes alias templates alongside class and variable
templates. Dependent alias families and alias partial specializations still
resolve through their legacy paths. The
`TemplateRegistry` owner-chain alias shim is
deleted: the extra `TemplateRegistry` owner-chain spellings (`Outer::Inner::Box`,
`ns::Outer::Box`, and parent-chain namespace suffixes such as
`inner::Outer::Inner::Box`) are no longer registered, and
`Parser::buildMemberClassTemplateAliasKeys` / `TemplateRegistry::registerTemplateAliases`
are gone. Member class templates register only the legacy owner-prefix key and
the simple member name; qualified spellings resolve through identity instead.
The legacy type-map and instantiation-cache bridge
derives an owner-derived instance key only for an actual same-spelling collision
between distinct class-owned primary templates.
`Parser::getClassTemplateInstanceKeyStem` resolves a full owner chain by
identity, or completes a lexical owner-chain suffix for a member spelling,
before `get_instantiated_class_name` and both class-instantiation-cache probes
consume the key. A collision adds `$td<TemplateDeclId>` before argument
hashing, so same-argument `OuterA::Inner::Box` and `OuterB::Inner::Box` cannot
share a type-map or cache entry; unambiguous and dependent spellings retain
their legacy base key. Namespace/global templates keep their previous cache keys. Dependent NTTP Spec
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
  requires a dependent qualifier kind). `Function`, `MemberObjectPointer`, and
  `MemberFunctionPointer` walks substitute return and parameter element types and
  member-pointer owner/pointee iteratively, rebuild the `FunctionParam` link list,
  preserve calling convention / cv / ref / variadic / plain noexcept / dll
  linkage and the opaque dependent-noexcept `ExprId`, and fail closed when a
  substituted return type, undecayed parameter, non-Record owner, or wrong
  member-pointer pointee category would result. Pack and template-template
  arguments remain deferred. Nodes remain 16 bytes.
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
- Remaining 3A work includes complete declarator interleaving and deletion of
  the flat semantic representation. The nested member-template Spec-rooted
  dependent stamping and the named qualified member-template chain consumers
  (instantiated-owner chains such as `Outer<int>::Box`, friend-of-member-template
  declarations, expression-side member-access/call owner materialization, and
  direct-member replay) now resolve through identity. Template-parameterized
  friend member class-template ids (`template <...> friend struct
  Outer<T>::Box<int>;`) now parse component-wise and bind their member primary by
  identity too. Instantiated-owner member alias type-ids
  (`Outer<Args>::Meter<…>`) resolve through identity in the
  `parse_type_specifier` arm as well. Instantiated-owner member variable
  expression forms resolve through identity after the member function-call
  probe. `CanonicalTypeTable::substitute` closes
  the function and member-pointer walk deferral. The `parse_type_specifier` `<`
  gate known-template test includes alias templates. The callable
  `MemberObjectPointer` adapter family is now migrated: the parser preserves the
  pre-rewrite pointee specifier that cast and non-type-template-parameter
  rewrites flatten, `importCanonicalMemberPointer` imports it structurally
  (fail-closed when absent or unsupported), and the MSVC mangler consumes the
  same preserved pointee. Namespace and global alias templates now publish
  declaration-layer identity: `parse_alias_template` publishes a
  `TemplateDeclId` under the namespace-mapped `OwnerId` and anchors the
  `TemplateAliasNode`, so their identity no longer depends on the registry
  spelling key. The full dependent-alias behavior, alias partials, and
  class-instantiation alias re-registration deletion remain deferred; select
  and bound the next still-Unmigrated dependent or template adapter family
  before expanding boundary-1 coverage. Stop here for review before starting
  another family, 3B, or the parallel frontend experiment.

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

Latest validation for the `<` gate alias arm: neutralizing the alias entries in
the known-template disambiguation test makes
`test_canonical_gate_alias_arm_dependent_member_ret0` fail to parse
`using Through = T::Meter<int>;` with "Expected ';' after alias template
declaration". With the arm present, that TU compiles and
`Outer::Meter<char>` materializes as `char`. Adjacent member-alias identity and
`test_less_in_base_class_ret0` (template-param base with non-template `<`
comparison) still pass. Linux sharded rebuild is warning-free;
`git diff --check` is clean. Fixed-corpus migration counters were not
re-measured on this Linux slice (no counter-touching choke points changed);
expect no movement.

Latest validation for namespace/global alias-template identity: the
`FrontendContext` doctest `Namespace and global alias templates publish
declaration identity` parses `namespace ns { template<typename T> using Meter =
T; }` plus a global alias and proves `findPrimaryAliasTemplate` under the
namespace and global `OwnerId` answers, that `primaryAliasPattern` yields the
same `TemplateAliasNode` the registry spelling key answers, that the two owners
are distinct, and that an unpublished name stays a fail-closed miss (16
assertions). Existing alias doctests still pass, so registry spelling keys and
lookup consumers are unchanged. Linux sharded rebuild is warning-free; the full
runner passed (3011 single-file + 12 multi-TU, 2995 runtime, 275 negative, 0
failures); the canonical architecture harness and the production adapter corpus
are unchanged; migration counters and the static dollar inventory stay within
baseline; `git diff --check` is clean.

Latest validation for member object pointer adapter recovery: a function
returning `decltype(static_cast<int MemberHost::*>(nullptr))` previously left
one Unmigrated declarator request; it now imports as a canonical
`MemberObjectPointer(owner, pointee)` (the production adapter corpus moves
25/0 to 26/0 supported/deferred, and the baseline is raised accordingly).
The native `checkAdapter` proves the recovered form equals the
declarator-shaped member object pointer and that a missing or unsupported
pointee stays fail-closed; the `adapter_member_object_pointee` mutation
(wrong pointee) makes the harness exit 1. The reduced regression
`test_canonical_member_object_pointer_decltype_ret0` keeps the parser shape
runnable and mixes the recovered flat form with a declarator member pointer.
Linux sharded rebuild is warning-free; the full runner passed (3011
single-file + 12 multi-TU, 275 negative, 0 failures); fixed-corpus migration
counters and the static dollar inventory stay within baseline.

Latest validation for instantiated-owner member variable identity: the
`FrontendContext` doctest `Instantiated-owner member variable resolves through
published identity` materializes `WideOwner<long long>` and
`NarrowOwner<char>`, then proves `findVariableTemplateByIdentityChain` on each
instantiated-owner `::Meter` spelling answers the owner's published variable
node (matching the `WideOwner::Meter` / `NarrowOwner::Meter` registry keys).
Mutation validation: neutralizing
`resolveOwnerChainClassOwner`'s injected-primary path makes
`REQUIRE(wide_variable.has_value())` fail. The end-to-end regression
`test_canonical_instantiated_owner_member_variable_template_identity_collision_ret0`
proves `WideOwner<long long>::Meter<Payload>` and
`NarrowOwner<char>::Meter<Payload>` instantiate distinct same-spelling
primaries (`sizeof(long long)` versus `sizeof(Payload)`). Adjacent member-
variable identity/stem and `test_member_var_template_ret42` regressions pass.
Linux sharded rebuild is warning-free; `git diff --check` is clean. Fixed-corpus
migration counters were not re-measured on this Linux slice (no counter-
touching choke points changed); expect no movement.

Latest validation for instantiated-owner member alias identity: the
`FrontendContext` doctest `Instantiated-owner member alias resolves through
published identity` materializes `WideOwner<long long>` and
`NarrowOwner<char>`, then proves `findAliasTemplateByIdentityChain` on each
instantiated-owner `::Meter` spelling answers the owner's published alias node
(matching the `WideOwner::Meter` / `NarrowOwner::Meter` registry keys).
Mutation validation: neutralizing
`resolveOwnerChainClassOwner`'s injected-primary path makes
`REQUIRE(wide_alias.has_value())` fail. The end-to-end regression
`test_canonical_instantiated_owner_member_alias_identity_collision_ret0` proves
`WideOwner<long long>::Meter<Payload>` and
`NarrowOwner<char>::Meter<Payload>` materialize distinct same-spelling
primaries (`char` versus `Payload`). Adjacent member-alias and instantiated-
owner class-template identity regressions pass. Linux sharded rebuild is
warning-free; `git diff --check` is clean. Class-instantiation alias
re-registration is intentionally kept for other consumers. Fixed-corpus
migration counters were not re-measured on this Linux slice (no counter-
touching choke points changed); expect no movement.

Latest validation for template-friend member identity: the `FrontendContext`
doctest `Template friend declaration resolves its member primary by identity`
parses `template <typename T> friend struct WideOwner<T>::WideBox<int>;` and the
same-spelling `NarrowOwner<T>::NarrowBox<int>` form and checks that each
`FriendDeclarationNode` uses the exact-specialization kind, resolves a non-null
member-primary declaration, and that the two same-spelling-bound primaries are
distinct AST nodes with `WideOwner::WideBox` / `NarrowOwner::NarrowBox` primary
spellings. `test_template_friend_member_identity_ret0` exercises a dependent owner
with a concrete argument, a concrete owner, and a dependent member argument end
to end. The three forms previously failed to compile with "Expected ';' after
template friend class declaration". The sharded MSVC rebuild is warning-free, the
full runner passed (3,025 single-file + 12 multi-TU, 275 negative, 0 failures),
and all migration counters and the static dollar inventory stay within baseline.
Note: private access through a member class-template friend is not granted even
for the plain concrete non-template form (the access check reports the private
member but the compiler still exits 0), so that enforcement is a separate
pre-existing defect, not part of this parse/identity slice.

Latest validation for callable substitution: the native architecture harness
(`tests/architecture/run_canonical_types.py`) builds `checkCallableSubstitution`
into `CanonicalTypeTests::run()` and proves that a function type substitutes its
return and parameter element types while keeping cv/ref, variadic, calling
convention, dll linkage, and the dependent-noexcept `ExprId`; that member function
and member object pointers substitute owner and pointee; that an interleaved
array/pointer/member-function-pointer/function declarator has one structural
result; that a foreign environment is untouched; that substitution of an already
concrete callable interns no new nodes; that a 65,536-deep pointer chain
substitutes without native recursion; and that a member object pointer whose
pointee substitutes to a function type is rejected. Five new mutation anchors
(`lost_substitute_function_return`, `lost_substitute_function_parameter`,
`lost_substitute_member_pointer_pointee`,
`lost_substitute_function_dependent_noexcept`, `lost_substitute_function_cv`)
each make the harness exit 1, so the walk, the owner/pointee split, and the
preserved metadata are all mutation-validated. The sharded MSVC rebuild is
warning-free, the 25-supported / zero-deferred canonical adapter corpus, all
migration counters, and the static dollar inventory stay within baseline, and the
full runner passed (3,024 single-file + 12 multi-TU, 275 negative, 0 failures).
The doctest translation unit that embeds the same header compiles under clang-cl
`/W4 /WX`.

Latest validation for nested callable declaration-builder identity: the parser-
level `DeclarationBuilder distinguishes nested function parameter signatures`
doctest proves overload declarations taking `void (*)(int)` and
`void (*)(double)` publish two entities through four Supported canonical
declarator requests and zero Unmigrated requests. Mutation validation forcing
callable parameters back through `matches_signature` collapses them to one
entity and makes the test fail. Sharded MSVC rebuild, the 25-supported / zero-
deferred canonical adapter corpus, all migration counters, the static dollar
inventory, and the full runner passed (3,022 single-file + 12 multi-TU, 275
negative, 0 failures). Follow-up end-to-end coverage proves `SymbolTable` keeps
the distinct overloads and conversion planning selects and invokes the matching
`int`, `double`, and record callback overloads. Function-identifier decay also
retains calling-convention and variadic metadata, including the canonical
adapter corpus's `__stdcall` callback. `SymbolTable` merge authority remains
intentionally unchanged.

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
  string-insertion-order independence are proved and mutation-validated for the
  canonical node families landed so far: builtin/cv/pointer/reference and array
  shapes; function and member-pointer shapes including calling convention,
  dll linkage, variadic, cv/ref, plain and dependent `noexcept`, parameter
  adjustment, and `ExpressionSubstitutor` substitution; `Record` / `Enum`
  `EntityId` identity, complete-object layout, and member/base field schemas;
  opaque `TemplateParameter`, `TemplateSpecialization`, `DependentName`, and
  `DependentTemplateMember` identity; production member-template identity
  publication (class, function, alias, and variable primaries) with qualified
  owner chains resolving through published IDs; and template-parameterized
  friend member class-template ids resolving their member primary by identity,
  instantiated-owner member alias type-ids resolving through identity in
  `parse_type_specifier`, instantiated-owner member variable expression
  forms resolving through identity after the member function-call probe, the
  `parse_type_specifier` `<` gate known-template test including alias
  templates, cast/NTTP member object pointer pointee recovery
  (`MemberObjectPointer` adapter family), and namespace/global alias-template
  primary identity publication.
  The landed-family inventory
  lives in `Current boundary and handoff`. Nested member-template Spec-rooted
  dependent stamping, unpublished/incomplete nominal, anonymous-union, and
  unpublished-base forms, the remaining dependent/template arguments, and
  deletion of the flat representation keep the criterion open.
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

- The `TemplateRegistry` owner-chain spelling shim is deleted; qualified
  member class-template type-ids resolve through `TemplateDeclId` in
  `parse_type_specifier` and instantiation, and primary member alias and
  variable templates publish `TemplateDeclId` identity under class-owned /
  template-owned OwnerIds with qualified spellings resolving identity-first
  (variable templates also get the `$td<TemplateDeclId>` instance-key stem).
  Richer adapters still block expanding boundary-1 shadow coverage (default
  arguments, exception specifications, fields, templates) or removing
  `SymbolTable` merge / `matches_signature` authority. Nested concrete callable
  parameters are proved canonical at the builder choke point and
  `CanonicalTypeTable::substitute` now walks function and member-pointer graphs,
  and instantiated-owner member alias type-ids resolve through identity, and
  instantiated-owner member variable expression forms resolve through identity,
  and the `<` gate known-template test includes alias templates, the callable
  `MemberObjectPointer` adapter family now imports the preserved cast/NTTP
  pointee structurally (with MSVC mangling recovery), and namespace/global
  alias templates publish declaration identity; select and bound the next
  still-Unmigrated dependent or template adapter family (dependent alias
  families or alias partials) before expanding that coverage.
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
- `SemanticAnalysis:*QueryTracksAnalysisState` fails on clean `main`; suspected
  shared-static cause is recorded in [known issues](KNOWN_ISSUES.md).
  Owner: sema query lifecycle.
- Scratch `allocateObject` can construct an object before destructor-vector
  registration throws `bad_alloc`, leaving its destructor unregistered. Fix
  allocator-failure exception safety before production nontrivial scratch probes.
  Owner: scratch object lifetime registration.
- Top-level expression fallback can mask declaration-parse errors; see
  [known issues](KNOWN_ISSUES.md). Owner: parser declaration dispatch.
