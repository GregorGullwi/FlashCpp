# Front-end migration progress

This is the handoff for agents continuing the migration. The
[rearchitecture plan](2026-08-24-front-end-rearchitecture-plan.md) is
authoritative for the design, boundaries, and exit criteria. This file records
the current boundary state, remaining work, and validation contract; completed
implementation history belongs in git and the regression suite.

## Next 3A slice

**Bind every pack referenced by a lockstep member-template expansion.**
The substitution layer now expands a pattern such as `Pair<Tail, Other>...` in
lockstep when every referenced pack is present in the effective binding.
Pattern deduction and effective owner bindings still collapse an enclosing pack
to a scalar or leave a second partial-specialization pack empty, so end-to-end
fields that mix packs from different template scopes remain deferred. Preserve
each referenced pack through pattern deduction and effective binding before the
expansion runs. Callable packs, non-type-parameter/alias/trailing-requires
member partials, opaque dependent values, and nested method emission remain
separate work; do not add evaluator or selection rules as part of this slice.

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
every referenced pack` test supplies two packs explicitly and verifies both
resulting `Pair` arguments. End-to-end binding of packs from distinct template
scopes remains the next slice.

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
`tryFindPublishedDerivedBaseOffset`, with the compatibility fallback centralized
behind that query. Binary and range call selection store the published
`BinaryOperatorCallTarget.declaring_type` rather than an AST owner `TypeIndex`;
codegen derives its ABI owner handle at that boundary.

Structural NTTP values are canonical through integral, bool, enum, named
bit-field, array, union-activity, float/double, null pointer-like, and
specialization member-pointer shapes. Class values carry ordered base/member
values with an explicit base count. Bit-fields normalize through declared type
and published width; array values preserve element type and outer bound; union
values key an explicit active variant or no-active state; float and double keep
typed exact values. Long double and non-null pointer payloads remain fail-closed
until their carriers can represent them. Canonical imports and tracing use
iterative worklists and the documented 4,096-level guards.

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
element instead of rebinding the bare pack element; multi-pack or still-dependent
patterns remain deferred. `test_canonical_member_nontrivial_type_pack_fields_ret0.cpp`
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
     canonical; the argument-bearing record query still reads `StructTypeInfo`
     through constructor overload resolution. Move the remaining selection and
     exception facts onto published constructor schemas, including variadic and
     omitted-default cases.
   - Template argument and substitution storage: move lazy constraint lookup from
     name-based parameters to depth/index parameters, and finish canonical
     preservation through NTTP reconstruction, deferred bases, stored arguments,
     member aliases, and non-projectable ordered declarators.
   - Conditional array decay, builtin unary `*`/`&`, builtin `->`, and builtin
     `new`/`new[]` now export canonical identities but still have documented flat
     projection refresh, nested-class, or unmigrated-shape gaps. Finish those
     consumers and remove the flat pointer/array fields they retain.
   - Migrate the separate compatibility candidate gathering in the mixed
     member/free selector; deleting `findUnaryOperatorOverloadCompatibility`
     alone does not remove that dependency.

3. **Complete importer and declarator coverage.** An ordered declarator around an
   alias that is an array, reference, member pointer, or function keeps that
   wrapper as the inner type. Remaining: a fully non-projectable ordered-spine
   `decltype` dereference (deferred to sema), and a call or xvalue operand whose
   value category the parser does not know at parse time.
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
  specialization, but a nested class of a primary instantiation still pops one
  flat pointer level, and an array object without a published id fails closed.
- The canonical record constructor schema is published independently of the field
  schema. A constructor whose parameter types do not import structurally is
  skipped; consumers fall back only when no constructor is importable. The field
  schema is still unpublished for anonymous-union and unimportable-member records
  and is not published for class-template specializations, which do publish the
  constructor schema.
- Nested classes of selected partial specializations now publish direct members,
  aliases, and non-deferred base layout. Deferred/template bases and other
  advanced nested-body effects still need the full instantiated-owner path.
- Class-template-owned direct, nested, and selected partial member
  specializations compose the full owner chain and keep member arguments
  separate. Concept-constrained type, concrete non-type literal, and pack member
  partials publish canonical parameter environments, and direct member function
  signatures, constructors, and bodies substitute effective owner bindings.
  Direct type-pack data-member template-ids publish concrete argument lists for
  namespace/non-template-class targets. Single nontrivial type-pack expansions
  substitute the generic element expression per concrete element, and lockstep
  expansion substitutes every referenced pack when the effective binding contains
  all of them. End-to-end binding of packs from distinct template scopes,
  callable-pack, non-type-parameter/alias/trailing-requires member partials,
  nested method emission, and other dependent owner/member arguments remain
  deferred.
- Non-null object pointer target identity, ambiguous/virtual-base member
  pointers, unpublished dependent bases, and the nested
  anonymous-union-in-anonymous-union hierarchy still need canonical subobject
  identity.

## Other open boundaries

- **Scratch rollback and publication:** `FrontendScratchTransaction` journals
  scratch state, `DeclarationBuilder`, `TemplateDeclTable`, bound `SymbolTable`
  publication maps, in-place global/namespace variable array type normalization,
  and namespace metadata. Nested commits stay provisional until the outer
  transaction commits; scope creation, cursor movement, and `ScopeRecord`
  publication are outside the boundary, production speculative parsing is not yet
  integrated, and the process-global namespace registry keeps transactions across
  multiple live `FrontendContext`s from being isolated. See
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
