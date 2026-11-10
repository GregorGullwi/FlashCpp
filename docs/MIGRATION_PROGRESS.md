# Front-end migration progress

This is the handoff for agents continuing the migration. The
[rearchitecture plan](2026-08-24-front-end-rearchitecture-plan.md) is
authoritative for the design, boundaries, and exit criteria. This file records
the current boundary state and validation contract; completed implementation
history belongs in git and the regression suite. Unresolved gaps are documented
only in [KNOWN_ISSUES](KNOWN_ISSUES.md); link there instead of restating them.

## Next 3A slice

**Separate provisional type publication from required instantiations.**
Value-pack substitution now stages its expressions until every element finishes,
then interns their identities atomically. Expansion appends to the caller's
result buffer and stages AST/ID pairs in one scratch allocation; batch interning
uses no temporary vectors. A later element's hard error publishes
no prefix of candidate expression IDs. Failed batch publication restores hash
buckets and live IDs, retains stable committed IDs, and reuses arena slots.
Required earlier class instantiations stay available and errors in separately
instantiated bodies still propagate, as required by C++20 [temp.deduct]. Native
checks cover the previous one-ID leak, successful publication, duplicate IDs,
late hashing failure, and rollback across an arena-chunk boundary.

Dependent-name canonical restamping now runs after semantic type substitution
and member-pointer owner rebinding succeed. Only a surviving stamp enters a
canonical-type transaction, which publishes after the replacement is applied.
Unsupported later bindings and failed restamps discard their imported type
prefix; required dependency instantiations stay outside that transaction.
Native guards cover late unsupported bindings, a class-body hard error, and a
1,024-segment qualifier path with stable repeated substitution identities.

Pure callable-component imports and structural substitution now publish through
a local canonical transaction. Callable environment imports also publish only
after every argument imports successfully; an unsupported later argument leaves
no bulk-import prefix. After that transaction ends, the legacy signature walk
may independently publish successfully substituted components. It still shares
nested callable objects and needs candidate-owned storage before whole-signature
rollback is safe. Native checks cover unsupported imports, an existing structural
rejection, and successful retry with committed identities.
Callable-component argument lists use one scoped scratch buffer of canonical
arguments, passed directly as a span. This avoids a temporary type-ID vector and
the type-ID overload's conversion buffer; success and failure restore the outer
scratch mark. A 32-argument regression checks repeated reuse and stable published
identities after the argument storage has been reclaimed.

Canonical owner publication now includes the enclosing owner import when stamping
a materialized member class-template type. Member-function-pointer owner probes
also discard incomplete argument imports and unchanged-owner work. A distinct
owner identity commits only when it is attached to the output type; required
owner materialization starts after the probe transaction has ended. Native guards
cover late failure, successful owner publication, and retained callback effects.

The legacy type registry now has a candidate-local publication journal. A frontend
publication transaction snapshots newly publishable top-level `TypeInfo` rows,
name bindings, and native-type bindings; commit promotes the snapshot, while
rollback restores overwritten bindings, removes new ones, and truncates the row
count. Nested commits remain provisional until the outer transaction commits.
Overlapping publication transactions from different live frontend contexts are
rejected before opening global journals; same-context nesting remains supported.
Admission now unwinds every successfully opened parser, namespace, legacy-type,
and table journal if a later acquisition fails, preserving any enclosing probe.
Legacy journal frames stay stable during growth; rollback swaps saved maps back
without constructing or moving a frame during exception cleanup.
Completion first validates participating journal ownership, nesting, and parser
rollback roots before closing or rewinding any component. Nested declaration
commits prepare parent undo capacity first, then transfer owned parameter keys
with no-throw moves so their payloads cannot allocate after canonical commit.
Scope creation and cursor movement now share that boundary: symbol-map undo runs
before provisional symbol-table slots and frontend `ScopeRecord`s are truncated.
Rollback restores the current, lookup, and declaring cursors; nested commits stay
provisional, and persistent-scope reset is rejected during an open transaction.
Bound symbol tables roll back their own unfinished journals before unregistering
on destruction, preserving undo of retained declaration changes. Context teardown
detaches surviving tables. Tables are pinned, and parser/test
resets use an explicit reset that rejects active table journals, detaches before
clearing local state, and preserves the context's retained scope records.
Scope entry preflights the proposed local scope ID and parent through the same
context validation used at publication. A conflicting table is rejected before
local scope/cursor changes or namespace creation/declaration; independent table
topologies remain unsupported.
Frontend scratch transactions also journal their context-owned diagnostic
records, severity counts, and note attachments to retained diagnostics. Nested
commits stay provisional, while template-context guards own their independent
stack. Structured `CompileError` values now own their text, referenced notes,
and instantiation context independently of the engine, including across copies,
moves, rollback, and engine destruction. Engine backing storage remains live for
borrowed diagnostic views; the separate parser `CompileContext` engine is not
automatically enlisted.
Scratch rollback now covers legacy type-registry rows and cold payloads, legacy
AST storage, Parser publication checkpoints, bound parser diagnostics, and
in-place rewrites for declaration, type-specifier, and mutable expression and
statement owners. The authoritative coverage and remaining speculative-parsing
gaps are maintained only in
[KNOWN_ISSUES](KNOWN_ISSUES.md#production-speculative-parsing-is-not-yet-integrated-with-frontend-scratch-transactions).
The type-pack materialization transaction and required-dependency isolation are
Boundary 8A work and remain deferred until Boundary 3A closes. Boundary 3A and 8A
remain incomplete.

Direct type-pack data members now retain empty/full packs, written CV/pointer
wrappers, enclosing-owner identity, and canonical subobject schemas. Coverage is
in `test_canonical_member_partial_type_pack_fields_ret0.cpp` and native
identity/boundary guards; removing declaration binding or expansion is rejected.
Single nontrivial patterns such as `Wrapper<Tail>...` now substitute the generic
type expression per pack element, including nested and pointer-wrapped wrappers,
while preserving the instantiated owner and canonical field type.
`test_canonical_member_nontrivial_type_pack_fields_ret0.cpp` covers empty/full
packs, owner-sensitive layout, and wrapper nesting; the native `Nontrivial member
pack fields retain wrapped elements and owners` test checks canonical owner and
member argument identity and rejects flattening the wrapper per element.
Lockstep expansion collects every bound pack referenced by the element pattern,
requires equal lengths and concrete elements, and substitutes one element from
each pack per result. The native `Lockstep member pack expansion substitutes
every referenced pack` test supplies two packs explicitly, verifies both
resulting `Pair` arguments, and checks the canonical owner and member-argument
identities. A same-spelling dependent name that is only available as a scalar
keeps the field dependent instead of materializing a wrong concrete type. The source regression
`test_enclosing_owner_pack_lockstep_member_field_ret0.cpp` and native canonical
checks cover empty, single-element, and mixed-type enclosing/member packs,
owner-sensitive layout, and ordered wrapper identities. `OuterTemplateBinding`
retains the existing binding representation in an out-of-line vector (24 bytes
of added record storage); its flattened fields are derived compatibility views.
Argument-list storage sharing also preserves exact legacy type-index projections
when semantic builtin identity considers indexed and unindexed arguments equal.
The adapter and spelling-based lookup remain scheduled for deletion at 8A.
Mixed scalar/pack element patterns now expand in lockstep as well: the scalar
argument is reused for every pack element while every pack is still required to
be bound and concrete. `test_lockstep_mixed_scalar_pack_member_field_ret0.cpp`
covers empty packs, owner-sensitive layout, and reuse of the scalar element, and
the native `Mixed scalar and pack member fields preserve canonical owners` test
checks the canonical owner and ordered `Pair` arguments. Single-pack
namespace-scope targets such as `List<Wrap<Tail>...>` now share the lockstep
helper; `test_namespace_scope_lockstep_pack_member_field_ret0.cpp` covers empty
and full packs, and the native `Namespace-scope lockstep targets preserve
canonical arguments` test checks the ordered wrapper arguments. Multi-pack
namespace-scope targets now consume the grouped effective environment:
`test_namespace_scope_lockstep_multipack_member_field_ret0.cpp` covers empty,
single-element, and full enclosing/member packs, and the native
`Namespace-scope multipack targets preserve canonical lockstep arguments` test
checks the ordered `Pair` arguments. Unequal referenced pack lengths now produce
`TemplatePackLengthMismatch` (#1820) for member and namespace member-field
expansions; `test_member_pack_length_mismatch_e1820.cpp` pins the diagnostic.

`TemplatePackExpansion.h` now shares iterative type/expression scanning,
current-versus-inner expansion classification, concrete/equal-length validation,
element iteration, and scalar binding overlays. Nested argument-list storage
preserves written expansion markers independently of semantic argument identity.
Inner expansions retain their complete binding even when the same pack also
appears in the outer expansion. Nested field, repeated-pack, deferred-base, and
NTTP-field regressions cover unequal inner/outer lengths, empty packs, mixed
sizes, and inner compile-time folds. Native checks pin nested canonical arguments,
syntax/schema agreement, storage metadata, and a 1,024-level iterative scan.
Runtime function-parameter fold replay and alias/variable-template flattened
adapters remain legacy paths scheduled for boundary 8A.

## Current state

Boundary **3A (canonical types)** is active and incomplete. The explicit-criteria
rollup is **10/79 complete**; the only completed 3A exit criterion is that
pointer-to-member overloads distinguish owner and pointee types. Passing tests or
the breadth of landed code do not complete the boundary.

Canonical inheritance is published as `TypeId` class-base schemas, including the
finalized ABI virtual-base table. Publication is transactional and rejects
conflicting order, duplicate targets, and self edges. AstToIr derived-to-base
address/pointer adjustments, explicit `static_cast` lowering, and IRConverter
validation consume `tryFindPublishedVirtualBaseIndex` and fail closed when a
virtual conversion has no published entry. `DerivedBaseConversionInfo` carries
only the relation kind; runtime and constructor offsets use
`tryFindPublishedDerivedBaseOffset`, which now requires the canonical class
graph and fails closed when either endpoint cannot be imported. Non-null object
and function pointer specialization arguments carry their published target
EntityId and function signature, and non-null member pointers carry their
canonical member value, so bases such as `Base<&global>`, `Base<&function>`, and
`Base<&Class::member>` import canonically. Binary and range call selection store the published
`BinaryOperatorCallTarget.declaring_type` rather than an AST owner `TypeIndex`;
codegen derives its ABI owner handle at that boundary.

Structural NTTP values are canonical through integral, bool, enum, named
bit-field, array, union-activity, float/double, null pointer-like, and
specialization member-pointer shapes. Class values carry ordered base/member
values with an explicit base count. Bit-fields normalize through declared type
and published width; array values preserve element type and outer bound; union
values key an explicit active variant or no-active state; float and double keep
typed exact values. Long double structural values are explicitly out of scope
until the compiler has real long-double support, so they stay fail-closed rather
than aliasing through double. Non-null pointer payloads remain fail-closed until
their carriers can represent them. Canonical imports and tracing use iterative
worklists and the documented 4,096-level guards.

Implicit copy/move construction and assignment are ODR-deferred. Semantic
analysis publishes selected special-member uses by owner `TypeId`, and codegen
drains that publication instead of inferring use from lowering paths. Template
bases without generated nodes receive unregistered copy/move nodes through the
same deferred queue. Constructibility has one shared authority,
`evaluateConstructibility`; zero-argument and argument-bearing paths agree on
canonical facts, with residual compatibility only where schemas are incomplete.

Selected full and partial class-template specializations share the primary
canonical identity where supported. Explicit full specializations publish the
primary `TemplateDeclId` and concrete `outer_template_args`; selected partial
nested classes preserve both their enclosing specialization and their own entity
identity. Class-template-owned member specializations compose the enclosing
class TypeId with the member template's own canonical arguments; the enclosing
owner is packed outside the member argument list, so owner and member arguments
remain separately observable. Nested member class templates recursively compose
each template-owned enclosing pattern, preserving the complete owner chain.
Selected member partial instantiations recover effective owner bindings from the
instantiated declaration and compose them with the actual member arguments on
their TypeInfo. Member partial patterns publish a canonical environment before
their base and body parse, so in-pattern member identities anchor to the pattern
instead of the primary or enclosing template. Data-member copy then substitutes
that pattern with the effective owner plus deduced local bindings for both layout
and the instantiated AST. Concept-constrained type parameters and concrete
non-type literal pattern arguments use the same environment because constraints
affect selection and concrete values already carry canonical identity. Pack
member partial patterns normalize each declared parameter, including the pack,
to one environment slot. Direct type-pack data-member template-ids expand from
the declared pack binding; outer binding views retain every element, and member
specializations split actual member arguments from the instantiated owner.
Supported materialized targets publish matching syntax, field, and subobject
TypeIds; class-template-owned targets publish `memberTemplateSpecialization` with
the instantiated owner TypeId separate from the member arguments. Dependent
qualified member-template fields with direct type-pack expansions use the same
resolved syntax for layout and the copied AST; unresolved or still-dependent
pack elements remain deferred. Scoped owner bindings restore the prior registry
entry after lookup. `buildAccumulatedOuterTemplateBindingInto` populates empty
caller-owned storage so this bridge does not place the large binding return
object in a recursive lookup frame. `tests/test_canonical_member_pack_instantiated_owner_ret0.cpp`
checks empty/full packs, owner-sensitive layout, and written pointer qualifiers;
the native identity guard checks syntax and record schema against the same
owner-bearing TypeId and rejects removal of the owner handoff.
`tests/test_canonical_member_template_owned_fields_ret0.cpp`
covers the runtime owner-sensitive field, and the native
`Member template specialization syntax retains its instantiated owner` test
covers same-arguments/different-owner identity plus the missing-owner mutation.
Nontrivial single-pack expansions recover the referenced pack from the element
pattern's type arguments and substitute the generic expression once per concrete
element instead of rebinding the bare pack element; still-dependent patterns
remain deferred. `test_canonical_member_nontrivial_type_pack_fields_ret0.cpp`
covers wrapped, nested, and pointer-wrapped element patterns plus owner-sensitive
layout, and the native `Nontrivial member pack fields retain wrapped elements and
owners` test pins the canonical owner and member argument list.
Direct member function signatures, constructor parameters, and bodies use the same
effective owner plus local bindings as data members.
Class-owned member specializations already encode their owner in the member
`TemplateDeclId`. Source-level nested declarations under partial specializations
are parsed and registered. The importer remains fail-closed for missing,
mismatched, unpublished, or unresolved owner facts.

## Remaining 3A work

1. **Make `TypeId` the conversion currency.** Finish replacing syntax-facing
   conversion ranking with structural planning. Remove compatibility selection
   where canonical import and declaration publication cover the source;
   implement substitution-aware ranking for unsupported callable conversions and
   user-defined constructor paths; complete derived-to-base and callable tails.
   Keep compatibility only for shapes the canonical importer or declaration
   bridge cannot represent, and audit those boundaries before deleting them.

2. **Migrate remaining flat consumers (independent parallel track).**
   - Constructibility: zero-argument variants and published record facts are
     canonical. The argument-bearing record query resolves exact matches,
     supported scalar and reference conversions, same-class by-value arguments,
     publicly derived by-value arguments whose base publishes a usable public
     copy or move constructor, unrelated class arguments with exactly one viable
     non-explicit public converting constructor, and omitted default arguments
     from published constructor schemas. Residual fallback shapes are tracked in
     [KNOWN_ISSUES](KNOWN_ISSUES.md). The nothrow query consumes published
     default-exception facts, including class constructor-call defaults with
     omitted nested defaults and the `NothrowDestructible` record fact. Implicit
     copy/move candidates
     cannot obscure a builtin argument's
     better direct converting constructor. Constructor schemas retain ellipsis
     and rank extra arguments through the ellipsis conversion sequence, including
     class types. Passing a class through `...` is conditionally supported, so
     constructibility adds no copy/move requirement and the selected
     constructor's exception specification decides the nothrow answer. Dependent
     extra arguments still defer.
   - Template argument and substitution storage: move lazy constraint lookup from
     name-based parameters to depth/index parameters, and finish canonical
     preservation through NTTP reconstruction, deferred bases, stored arguments,
     member aliases, and non-projectable ordered declarators. Member
     class-template base lists now accept pack-expanded bases such as `List : U...`,
     and a selected partial owner resolves its own nested member class templates
     instead of falling back to the primary owner's template. Class-type static
     constexpr member arguments now evaluate their brace initializer through the
     aggregate-construction path, so the typed value identity participates in
     outer specialization matching. Ordinary call candidate sorting now uses the
     canonical `[temp.deduct.partial]` comparison as a tie-break when structural
     specificity scores are equal, and non-SFINAE tied candidate sets fail closed
     when no unique maximal template remains instead of using lookup order. Alias
     targets now promote their flat
     pointer/reference shape to the ordered declarator spine before substitution,
     so pointer-to-reference alias instantiations are diagnosed while valid
     reference-to-pointer aliases still resolve.
    - Conditional array decay, builtin unary `*`/`&`, builtin `->`, and builtin
      `new`/`new[]` now export canonical identities but still have documented flat
      projection refresh, nested-class, or unmigrated-shape gaps. Finish those
      consumers and remove the flat pointer/array fields they retain.
    - A first split migrates array-of-pointer element sizing in array subscript
      lowering and `setMaterializedTypeSpecifierSize` from flat `pointer_depth()`
      to `runtime_pointer_depth()`. Remaining flat readers in this family
      (matches_signature pointer levels, range-for iterator object type, and
      non-projectable ordered spines) are deferred.
   - Interleaved pointer/array member storage now sizes and aligns from the
     ordered declarator spine, and constexpr member typing prefers the published
     member `TypeId`. Local array objects also allocate from the ordered element
     count and element size, and references to such arrays identify the array
     address rather than dereferencing it as a value. Value `auto`, `auto&`, and
     `auto*` deduction keep the ordered spine through array decay and address-of,
     and array-object lowering reads the published canonical `TypeId`
     instead of the ordered spine. Instantiated reference-to-array parameter
     bounds are materialized onto the type specifier so canonical identity sees
     the array. Array-member lvalue-address lowering now emits the member address
     for every array member, so direct member-array reference arguments receive
     a real address. Conditional array branches consume their annotated
     array-to-pointer conversion without re-dereferencing the address temp,
     subscript lowering treats pointer-to-array declarators as scalar pointers
     rather than array objects, builtin unary `*` consumes the canonical
     indirection result, and pointer arithmetic scales by the canonical pointee
     size after ordered array decay. Identifier and qualified-global array
     detection now consume the sema canonical predicates instead of projection
     fields. A published member-pointer access adopts that member's canonical
     type, and a published class-to-pointer subscript conversion adopts the
     pointer's pointee. A pointer with no structural pointee still peels one
     flat level. Other flat readers still need migration before the projection
     fields can be removed.
   - Class-scope using-declarations now retain their target base and member
     spelling on `StructDeclarationNode` and copy that metadata into
     instantiated class declarations. Unary `+`, `-`, `~`, and `->` lookup
     consume it, so same-named base operators are disambiguated by the
     using-declaration instead of all bases contributing candidates. General
     member using-import lookup remains unsupported.
   - Migrate the separate compatibility candidate gathering in the mixed
     member/free selector; deleting `findUnaryOperatorOverloadCompatibility`
     alone does not remove that dependency.

3. **Complete importer and declarator coverage.** Partial-specialization bodies
   now reuse the ordinary anonymous-union member parser, so union fields and
   layout are published for selected specializations. An ordered declarator
   around an alias that is an array, reference, member pointer, or function keeps that
   wrapper as the inner type. A non-projectable ordered pointer dereference keeps
   that spine and yields an lvalue of the pointee, and a call or xvalue operand
   keeps the value category [expr.call] and [expr.ref] give it.
   `UnsupportedStaticMemberType` (1020) remains the fail-closed guard for
   canonical type families not yet imported.

4. **Close and mutation-validate the 3A exit criteria.** Prove independence from
   parser/context stacks, parse order, and string-table insertion order; cover
   the remaining pointer-to-member, function, dependent, and template families;
   and remove the flat pointer-level and array-dimension fields from migrated
   semantic paths.

Before declaring 3A complete, check every exit criterion in the plan and read its
[parallel architecture experiment](2026-08-24-parallel-front-end-architecture-experiment.md).
Boundary 3B (ABI-conforming mangling) and boundary 4 (authoritative expression
sema) follow 3A. Do not route around unfinished canonical-type ownership to start
them.

### Bounded unsupported cases

These seed adjacent slices; the authoritative inventory is
[known issues](KNOWN_ISSUES.md).

- Constraints: complex non-type concept argument expressions, constraints over
  packs or arrays, member/function dependent decorations, and forms that cannot
  be normalized leave candidates unordered. Constrained free-function templates,
  packs, and non-type template parameters are unsupported in address deduction;
  complex NTTP expressions, non-literal dependent arguments, non-type packs, and
  a C-style ellipsis combined with a pack are unsupported.
- Evaluated function-template address arguments still need IR materialization of
  the selected specialization.
- Virtual member-function addresses and multiple-inheritance member-pointer
  adjustment are unsupported, and calling through a materialized member-function
  pointer is unimplemented.
- Conditional array decay and builtin subscript element extraction are
  structural-only. An implicit class-template specialization's `*this` uses that
  specialization. A pointer to a nested class of that specialization is a
  nested record under the specialization. A dependent class-template pattern,
  or an instantiation whose owner cannot be imported, still pops one flat
  pointer level, and an array object without a published id fails closed.
- The canonical record constructor schema is published independently of the field
  schema. A constructor whose parameter types do not import structurally is
  skipped; consumers fall back only when no constructor is importable. The field
  schema is still unpublished for anonymous-union and unimportable-member records
  and is not published for class-template specializations, which do publish the
  constructor schema.
- Nested classes of selected partial specializations now publish direct members,
  aliases, replayed dependent deferred bases, default member initializers, and
  base default construction. Other advanced nested-body effects still need the
  full instantiated-owner path.
- Built-in operator classification for arithmetic, bitwise, shift, comparison,
  and logical operators is shared through `TokenKind.h`.
- Same-spelled class and member templates under distinct owners now keep
  distinct instantiation cache keys and generated names through their qualified
  owner spellings rather than numeric `TemplateDeclId` suffixes.
- Member alias target materialization now keeps grouped pack bindings, so
  adjacent expansions such as `Pack<A..., B...>` preserve argument boundaries
  for empty, trailing-empty, and multi-element packs.
- Unary `+`, `-`, and `~` now publish the promoted `int` result type through
  parser expression typing and constant evaluation, so `auto`, `decltype`, and
  call deduction observe the same integral promotion as semantic analysis.
- Class-template-owned direct, nested, and selected partial member
  specializations compose the full owner chain and keep member arguments
  separate. Concept-constrained type, concrete non-type literal, and pack member
  partials publish canonical parameter environments, and direct member function
  signatures, constructors, and bodies substitute effective owner bindings.
  Direct type-pack data-member template-ids publish concrete argument lists for
  namespace/non-template-class targets. Single nontrivial type-pack expansions
  substitute the generic element expression per concrete element, and lockstep
  expansion substitutes every referenced pack when the effective binding contains
  all of them, including enclosing and local packs retained as separate binding groups.
  Callable member declarations normalize legacy return-type/pointer function-pointer
  arguments to canonical callable identity for partial-specialization deduction.
  Callable-pack, non-type-parameter/alias/trailing-requires member partials and
  other dependent owner/member arguments remain deferred. Nested classes inside
  selected member partials now retain and publish their method bodies through
  source aliases for both global and namespace-qualified owners. Declaration
  namespace resolution is centralized for class-owned template names.
- Non-null object pointer target identity, ambiguous/virtual-base member
  pointers, unpublished dependent bases, and the nested
  anonymous-union-in-anonymous-union hierarchy still need canonical subobject
  identity.

## Other open boundaries

- **Scratch rollback and publication:** `FrontendScratchTransaction` journals
  scratch state, candidate diagnostic records and note attachments in both the
  frontend and bound Parser engines,
  `DeclarationBuilder`, `TemplateDeclTable`, legacy type-registry
  rows and name/native bindings, index-keyed cold type payload stores, and
  pointer-keyed legacy `StructTypeInfo`/`EnumTypeInfo` arenas (unreachable ranges
  truncate; ranges referenced by a surviving row are retained), bound
  `SymbolTable` publication maps, pre-existing `TypeInfo` row snapshots,
  in-place global/namespace variable array type normalization, namespace
  metadata, the active Parser's cursor, append-only root publication, parse
  context stacks, class-template pack and lambda-capture stacks, pending explicit
  template arguments, current function/linkage, calling-convention state,
  pre-existing `QualifiedIdentifierNode` field rewrites, AST owner
  outer-template binding rewrites, and whole-declaration snapshots for
  pre-existing `StructDeclarationNode`, `FunctionDeclarationNode`, standalone
  `DeclarationNode`, `EnumDeclarationNode`, helper and template declarations,
  standalone/template variable declarations, friend declarations, and
  pre-existing `TypeSpecifierNode` shapes.
  Scope creation, cursor movement, and `ScopeRecord` publication roll back
  together after symbol-map undo. Nested commits stay
  provisional until the outer transaction commits; production speculative parsing
  is not yet integrated. The namespace and type-registry journals remain process-global;
  overlapping frontend transactions from different contexts are rejected, but
  ordinary writes from another context are not isolated. See
  [known issues](KNOWN_ISSUES.md).
- **Compiler bugs and bounded unsupported cases:** consult
  [known issues](KNOWN_ISSUES.md) before selecting adjacent work; add newly found
  bugs there.
- **Template-instantiation stack safety:** class-base preflight schedules plain
  nested-class member-type chains ending in a class-template-specialization alias
  (`tests/test_deep_nested_member_type_base_chain_ret0.cpp`); member-template
  segments and indirect alias targets remain open, and boundary 8A's AST-based
  instantiation and old-path deletion criteria are unfinished.
- **Callable ABI mangling:** MSVC and Itanium encode ordered callable wrappers
  and structured signatures, including function references and pointer returns.
  MSVC retains `tryMaterializeFlatCallableForMangling` for projectable shapes.
  Full ABI conformance remains boundary 3B; preserve calling-convention identity
  while completing that audit.
- **Boundary 4 handoff - member-function-pointer descriptor identity:** a
  concrete member-function-pointer descriptor now reconstructs without the
  compatibility projection. `canonicalizeType` stamps the published class
  identity onto the signature's structured return and parameter components
  (`bindPublishedClassComponents`), and `tryImportCanonicalTypeDesc` binds the
  owner spelling carried by the signature before importing, so
  `materializeTypeSpecifier` round-trips the member pointer. Remaining: dependent
  address NTTP cases still return `Unresolved`, and the identity stamp should
  move into the remaining signature writers so the centralized step can be
  dropped.
- **Negative tests:** encode the exact expected diagnostic ID multiset in the
  filename (`_e1001.cpp`, `_e1003_e1051.cpp`); `_fail.cpp` is reserved for the
  immutable legacy inventory.

## Active findings

- The top-level expression fallback can mask declaration-parse errors; see
  [known issues](KNOWN_ISSUES.md).
- Bit-field declarations still accept non-integral types, widths larger than the
  declared type, and named zero-width fields; static constexpr data-member NTTP
  arguments also fail outer specialization matching. Both are recorded in
  [known issues](KNOWN_ISSUES.md).

## Validation handoff

After compiler-source changes, build with `.\build_flashcpp.bat`; run focused
regressions while iterating, then `pwsh tests/run_all_tests.ps1` when ready.
Never run the full suite concurrently with the build. On Linux, the equivalent
native build is `make test -j4 CXXFLAGS='-std=c++20 -Wall -Wextra -Wshadow
-Werror -Wno-missing-braces'`.

Retain the native end-to-end pointer-to-reference diagnostic guard: parenthesized,
chained, and nested reference aliases must report #1001 at the pointer token;
valid function-returning-reference declarators must still parse.

After canonical identity changes, run
`python tests/architecture/run_canonical_types.py --mutations`; update changed
anchors while preserving each mutation's required rejection.

Run the host-native migration-counter and identity-inventory scripts under
`tests/migration_counters/` after compiler changes and keep every fixed-corpus
entry within baseline. `canonical_structural_trait_fallback` is 0 on the
structural-trait and lazy-constraint regressions; residual lazy-constraint
fallbacks are the constructibility probes, which need the canonical
constructor-query path. The inline dollar-recovery inventory and canonical-adapter
source corpus remain within their supported/deferred baselines. The fixed adapter
corpus remains 26 supported and zero deferred. Gate 0's Windows and ELF
multi-translation-unit checks remain required compatibility evidence. See the
plan for complete boundary-specific validation.

For recursive-path changes, report the largest changed native stack frame and
whether stack use stays bounded as logical depth grows; do not raise the stack
limit to make a regression pass. Canonical traversal is iterative where noted,
but surrounding parser/substitution/lowering paths still recurse and do not
establish bounded native stack use for the whole template pipeline. Retain the
1 MiB native callable and canonical worklist guards, the 128-level alias limit,
and the 4,096-level import/trace guards. Current measured sizes to preserve:
`StructMember` is 320 bytes and `TypeSpecifierNode` is 600 bytes on the measured
Windows x64 build.

### Linux member-type CI continuation

The Linux pointer-reference overload failure exposed an Itanium mangling
collision: the legacy encoder emitted pointer constructors before references and
discarded referent pointer CV as if it were a by-value parameter qualifier. It now
emits outer constructors first, traverses legacy pointer levels from outermost to
innermost, and retains CV through references. Ordered and legacy encoders share
the ABI qualifier ordering. Native exact-encoding checks include 4,096 pointer
levels in both representations under the normal test stack.

The callable-array regression exposed a missing aggregate arithmetic conversion.
`Holder<long long>{int_value}` tagged the source as a 64-bit member store while
leaving its producer 32-bit. Shared nested, array-member, default-member, and
synthesized default-constructor stores now use the existing numeric conversion
helper. The local aggregate store boundary still needs migration, as documented
in `KNOWN_ISSUES.md`; nested aggregate and contextual-bool gaps are recorded there
too.
