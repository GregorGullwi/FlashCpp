# Front-end migration progress

Current state for the authoritative
[front-end rearchitecture plan](2026-08-24-front-end-rearchitecture-plan.md).
Keep completed work concise; earlier implementation and validation details are
recoverable from git history. Replace stale state rather than appending history.

Last updated: 2026-09-21. Boundary 3A now has an outermost-to-innermost
`DeclaratorComponent` spine on `TypeSpecifierNode`. Named and abstract
pointer/array declarators use an explicit frame stack, so forms including
`int (*(*p)[3])[4]`, deeper pointer/array alternation, and pointer cv at each
level import losslessly into the existing recursive `CanonicalTypeTable`.
The adapter exports pointer/array/reference wrapper chains iteratively, and
import-export-import preserves canonical identity independently of unrelated
canonical insertion order. Legacy pointer/array fields are rebuilt only for
the two exactly projectable boundary shapes; mixed interleavings are marked
non-projectable.

The bounded `sizeof`/size and unary dereference/address route now walks the
ordered/canonical structure. `CanonicalTypeDesc` carries a temporary `TypeId`
bridge for these non-projectable shapes. MSVC/Itanium mangling consumes the
ordered declarator, and general overload/conversion resolution now consumes
the ordered spine for exact structural identity: a non-projectable argument
compares its declarator component sequence plus resolved base type, base cv,
and callable payload, an lvalue's value-category reference qualifier is
stripped, and a null pointer constant still converts to an ordered pointer.
Conversion families outside that boundary (array/function decay,
qualification adjustment, `void*`, derived-to-base, ordered reference
binding, and callable-component conversion) stay deferred and fail closed as
an ordinary no-match instead of aborting compilation. IR lowering of ordered
pointer values is still unmigrated. Callable and member-pointer components are
named by the spine format, but their cold `FunctionSignature`/owner payload
export and the remaining template, traits, constexpr, and IR consumers are not
migrated.
`DeclaratorComponent` is 16 bytes; the cold vector plus projection-state field
increased `TypeSpecifierNode` to 520 bytes in the canonical architecture
probe. Clang stack-usage reports `parse_declarator` at 5,160 bytes versus
5,000 bytes on `origin/main`; nested declarator depth is carried by heap-backed
frames and does not increase native call depth. The next slices are the
deferred conversion families (qualification adjustment and array/function
decay, then `void*` and ordered reference binding) and IR lowering of ordered
pointer values, followed by removal of the flat pointer/array reads.

Immediately before this slice, direct member alias targets that capture an enclosing
class-template parameter can publish with separate owner and alias declaration
IDs. Direct type targets recover the owner parameter's declaration and index
from the enclosing parser parameter scope before canonical import. The
canonical table resolves an explicitly identified direct member alias
with owner specialization arguments followed by alias arguments, using the
existing iterative substitution worklist. That resolution is fail-closed: a
dependent (partially concrete) owner or alias argument, a non-Type owner or
member argument layout, or an owner specialization that does not cover every
owner parameter reference the published target performs returns no target
instead of substituting a short layout or emitting a partially dependent type.
Qualified member-alias uses now stamp a
published member alias `TemplateDeclId` into a distinct `DependentMemberAlias`
canonical node rather than an identity-free `DependentTemplateMember`.
`CanonicalTypeTable::resolveMemberAliasUse` takes the owner
specialization from the node's qualifier and the alias arguments from its
type-only argument chain and redirects through the same owner-then-alias worklist
as `resolveMemberAliasTarget`; `tryResolveDependentTip` applies it at the
substitution choke point, so a concrete `Captures<int>::template Pointer<char>`
collapses to the published target while an unknown declaration or a
partially-dependent argument stays a dependent tip. A nested owner+alias
argument target such as
`Both<Owner, Value>` now publishes: after the member alias declaration ID
exists, a bounded post-publication pass stamps each matching target type-
argument specifier with the owner or alias declaration identity and index, so
the target imports and resolves with owner arguments followed by alias
arguments.

Published namespace/global and member alias primaries retain their `TemplateDeclId` on
the `TemplateAliasNode`; dependent alias uses stamp that ID and ordered
arguments, and importable direct targets publish a canonical pattern under that
declaration ID together with its declared type/non-type/template argument
layout. Dependent alias uses now stamp mixed arguments when every argument is
representable: type arguments that name a published active parameter carry its
`TemplateDeclId` and index, concrete published primary class templates used as
template-template arguments carry their `TemplateDeclId`, active
template-template parameters keep owner plus index, dependent non-type
expressions intern to a stable `ExprId`, and stampable bool/unsigned-integral
literal arguments intern their call-site syntax node. Other non-type forms have
no expression node, so a call site with one keeps the whole canonical stamp
deferred. Concrete direct alias specializations now redirect to the
published target for both type-only and mixed layouts: each argument is matched
positionally against the published parameter kinds, opaque NTTP `ExprId` and
template-template `TemplateDeclId` identities are preserved, and an alias
target that names its own template-template parameter by owner and index is
rebuilt with the concrete `TemplateDeclId`. Chains resolve iteratively with
declaration-ID cycle detection, and concrete direct alias specializations nested
anywhere in the substituted graph — wrappers, function and member-pointer
shapes, specialization type arguments, and nested array/qualified/pointer/
reference nodes — now normalize to their published targets through the same
explicit worklist, with the declaration-ID scope carried through each nested
expansion so nested cycles stop at the alias boundary. The canonical adapter
imports a distinct
`AliasTemplateSpecialization` node rather than a class specialization or a
registry spelling identity. Dependent type arguments, argument-kind and arity
mismatches, targets that mention non-type arguments while the alias declares a
non-type parameter, dependent template-template arguments, targets with an
unknown enclosing environment, and dependent-member targets remain deferred. A
direct alias target that names a non-type parameter is
rejected with `NonTypeAliasTargetUnsupported` (1812), alias partial or explicit
specialization syntax is rejected with
`AliasTemplateSpecializationForbidden` (1813), and a directly self-referential
alias declaration is rejected with `RecursiveAliasTemplateInstantiation` (1814);
their canonical representations remain unsupported. Wrong alias use arity reports
`AliasTemplateArityMismatch` (1815), with defaults and packs accepted at the use
site. Indirect alias recursion is
bounded by an implementation-limit guard that reports
`AliasInstantiationDepthExceeded` (3002) instead of overflowing the native
stack. Earlier on
`main`: the deferred `MemberObjectPointer`
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
still fail closed there). Primary member alias templates publish identity and
direct canonical targets the same way: `parse_member_template_alias` publishes a `TemplateDeclId`
(`TemplateDeclTable` `PrimaryKind::Alias`) under the class-owned OwnerId from
the enclosing parse-time EntityId, or a template-owned OwnerId for direct
members of published namespace/global primary class templates, anchoring the
`TemplateAliasNode` under that id. Directly importable targets stamp their own
Type-kind parameters with that member alias id and publish the target plus its
argument layout and known enclosing template ID under the same canonical
declaration key. `resolveMemberAliasTarget` resolves such a target by
substituting the owner environment and then the alias environment through the
iterative worklist, and is fail-closed: a dependent owner or alias argument, a
non-Type owner or member argument layout, or an owner specialization that does
not cover the target's owner parameter references returns `std::nullopt` rather
than building a partially dependent type or reaching the worklist's arity
throw. A nested owner+alias argument target `Both<Owner, Value>` publishes
through a bounded post-publication pass that stamps matching target type-
argument specifiers with the owner or alias declaration identity. Unresolved,
dependent-member, and partial targets remain deferred.
Qualified member alias type-ids resolve
through `Parser::findAliasTemplateBySpelling` (identity chain first via
`findAliasTemplateByIdentityChain`, registry alias lookup fail-closed
fallback) at the `parse_type_specifier` alias lookups and at the shared
materialization choke point `materializeAliasTemplateInstantiation`, so
partial-namespace-suffix spellings such as `m::Gauge::Meter<int>` (class
`Gauge` in namespace `n::m`) that missed the registration-time owner-qualified
registry key now resolve through the owner chain's type-system lookup. Dependent
qualified member-alias uses stamp that same published `TemplateDeclId` into a
canonical `DependentMemberAlias` node, so `ExpressionSubstitutor` auto-redirects
them once the owner qualifier is concrete. Member
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
  gate known-template test includes alias templates. General
  overload/conversion resolution now consumes the ordered declarator spine for
  non-projectable interleaved shapes: exact component/base/cv/callable identity
  resolves, an lvalue argument's value-category reference qualifier is stripped,
  a null pointer constant still reaches an ordered pointer, and everything else
  fails closed as an ordinary no-match instead of aborting at the flat
  projection guard. Array/function decay, qualification adjustment, `void*`,
  derived-to-base, ordered reference binding, and callable-component conversion
  remain deferred. The callable
  `MemberObjectPointer` adapter family is now migrated: the parser preserves the
  pre-rewrite pointee specifier that cast and non-type-template-parameter
  rewrites flatten, `importCanonicalMemberPointer` imports it structurally
  (fail-closed when absent or unsupported), and the MSVC mangler consumes the
  same preserved pointee. Namespace and global alias templates now publish
  declaration-layer identity: `parse_alias_template` publishes a
  `TemplateDeclId` under the namespace-mapped `OwnerId` and anchors the
  `TemplateAliasNode`, so their identity no longer depends on the registry
  spelling key. The full dependent-alias behavior, alias partials, and
  class-instantiation alias re-registration deletion remain deferred. The next
  still-Unmigrated 3A slice is the deferred conversion families and IR lowering
  of ordered pointer values before the flat pointer/array reads are removed, or
  a bound dependent/template adapter family. Stop here for review before
  starting another family, 3B, or the parallel frontend experiment.

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

Completed validation anchors remain in the source and architecture suites:

| Capability | Regression anchor |
|------------|-------------------|
| Owner-capturing and nested member aliases | `checkMemberAliasOwnerEnvironmentFailClosed`, `checkMemberAliasOwnerNestedTarget`, `test_canonical_member_alias_nested_target_ret42` |
| Alias `<` disambiguation | `test_canonical_gate_alias_arm_dependent_member_ret0`, `test_less_in_base_class_ret0` |
| Namespace/global alias identity | `Namespace and global alias templates publish declaration identity` doctest |
| Member-object pointer adapter | `checkAdapter`, `test_canonical_member_object_pointer_decltype_ret0` |
| Instantiated-owner member variable and alias identities | `test_canonical_instantiated_owner_member_variable_template_identity_collision_ret0`, `test_canonical_instantiated_owner_member_alias_identity_collision_ret0` |
| Template-friend member identity | `test_template_friend_member_identity_ret0` |
| Callable substitution and bounded recursion | `checkCallableSubstitution` (including a 65,536-deep pointer chain) |
| Nested callable declaration identity | `DeclarationBuilder distinguishes nested function parameter signatures` doctest |
| Ordered declarator overload/conversion | `test_interleaved_pointer_array_argument_ret42`, `test_interleaved_pointer_array_argument_shape_e1704` |

The owner-alias tests also cover dependent/non-Type arguments and incomplete
owner environments failing closed. Member class-template friend access
enforcement remains open; see `KNOWN_ISSUES.md`. The earlier per-change
mutation results, historical runner totals, and platform build reports are
recoverable from git history.

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
  (`MemberObjectPointer` adapter family), namespace/global alias-template
  primary identity publication, and general overload/conversion resolution
  consuming the ordered declarator spine for exact interleaved-shape identity
  (with the deferred conversion families failing closed).
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
  alias templates publish declaration identity. Dependent namespace/global alias
  uses now stamp a published `AliasTemplateSpecialization` identity through the
  canonical adapter for type-only arguments and, when every argument is
  representable,   for mixed layouts: dependent type arguments carry their active
  parameter `TemplateDeclId` + index, concrete published primary class templates
  carry their `TemplateDeclId`, active template-template parameters keep
  owner + index, dependent non-type expressions intern to a stable `ExprId`, and
  stampable bool/unsigned-integral literal arguments intern their call-site
  syntax node. Other non-type forms have no expression node, so one at a call
  site keeps that whole canonical stamp deferred. Direct
  alias targets now redirect after concrete substitution for both type-only and
  mixed layouts: argument identity kinds are matched
  positionally against the published parameter kinds, `ExprId` / `TemplateDeclId`
  payloads survive, and targets naming a template-template parameter by
  owner/index rebuild with the concrete `TemplateDeclId` while chain resolution
  stays iterative with declaration-ID cycle detection. Non-type argument
  references in a target cannot be distinguished from literals, so such targets
  stay deferred once the alias declares a non-type parameter, along with member
  dependent aliases and alias partials. Nested owner+alias argument member-alias
  targets (`Both<Owner, Value>`) now publish through a bounded post-publication
  stamping pass. The
  member-alias resolver is fail-closed against dependent and non-Type argument
  layouts and uncovered owner arity, returning no target instead of substituting.
  Unsupported alias shapes now carry
  structured diagnostics: a direct alias target that names a non-type parameter
  reports `NonTypeAliasTargetUnsupported` (1812), alias partial/explicit
  specialization syntax reports `AliasTemplateSpecializationForbidden` (1813),
  and a directly self-referential alias declaration reports
  `RecursiveAliasTemplateInstantiation` (1814), each covered by an exact-ID
  negative test. Alias uses with too few required or too many fixed arguments
  report `AliasTemplateArityMismatch` (1815); defaults and packs are accepted.
  TODO: investigate value initialization through a defaulted alias (`J<> z = 42`),
  which compiles but currently yields an incorrect runtime value.
  Indirect alias recursion is bounded by a logical-depth guard on
  alias materialization that reports `AliasInstantiationDepthExceeded` (3002)
  rather than overflowing the native stack. Latest architecture validation: the
  native `CanonicalTypeTests` `checkAliasRedirection` case exercises mixed
  layouts, unresolved-identity distinction, layout conflict rejection,
  template-template placeholder replacement, mixed chain resolution, self and
  mutual cycles, and the arity/kind/dependent/non-type fail-closed boundaries.
  Source regressions `test_canonical_direct_alias_mixed_params_ret42`,
  `test_canonical_direct_alias_mixed_template_arg_ret42`,
  `test_canonical_direct_alias_mixed_nttp_arg_ret42`, and
  `test_canonical_direct_alias_mixed_literal_nttp_arg_ret42` cover the ready
  non-type, template-template, dependent non-type, and literal non-type mixed
  direct-alias materialization paths; trace probes confirmed
  `buildDependentAliasTemplateTypeSpecifier` enters the mixed stamp with both
  arguments representable in each case. Alias targets that are arrays now
  materialize as array objects: `using A = int[3];` and
  `template <class T> using M = T[2][3];` carry their extents on the alias type
  specifier, which the declaration path mirrors into the declaration so
  `M<int> obj;` gets the correct `sizeof`, initializer, and element access
  (`test_alias_array_object_ret42`); a malformed bound reports the existing
  1003/1051 bracket diagnostic. Dependent alias-template bounds retain their
  expressions until concrete substitution, so `T[N]` and `T[N + 1]` have the
  correct size, initializer, and element access
  (`alias_template_dependent_bound_ret42`). Nonpositive or unresolved concrete
  bounds report `AliasTemplateArrayBoundUnresolved` (1817). Declaration
  synthesis keeps pointer-to-array objects scalar
  (`decltype_pointer_array_scalar_ret42`). One-dimensional alias-array function
  parameters adjust to pointers, and calls apply array-to-pointer decay
  (`alias_array_parameter_decay_ret42`). Concrete multidimensional parameters
  now preserve their inner bounds and lower subscripts through a flattened
  pointer-row access, including direct, alias-template, and explicit
  pointer-to-array parameter forms (`multidimensional_array_parameter_ret42`,
  `alias_multidimensional_parameter_ret42`, `pointer_array_parameter_ret42`).
  Dependent inner bounds in function templates retain their expressions until
  concrete substitution, so `int a[2][N]` and `int a[2][N][M]` index with the
  same flattened pointer-row shape as a directly written parameter
  (`function_template_dependent_array_bound_ret42`,
  `function_template_dependent_inner_bounds_ret42`). Nonpositive or unresolved
  instantiated bounds report `FunctionTemplateArrayBoundUnresolved` (1818).
  Parenthesized pointer-to-array alias
  template targets remain unsupported and report
  `UnsupportedAliasTemplateTargetDeclarator` (1816). Select and bound one of
  the remaining families before expanding boundary-1 coverage.
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
