# Front-end migration progress

This is the handoff for agents continuing the migration. The
[rearchitecture plan](2026-08-24-front-end-rearchitecture-plan.md) is
authoritative for the design, boundaries, and exit criteria. This file records
the current boundary state, the remaining work, and the validation contract;
completed implementation history belongs in git.

## Current state

Boundary **3A (canonical types)** is active and incomplete. The explicit-criteria
rollup is **10/79 complete**; the only completed 3A exit criterion is that
pointer-to-member overloads distinguish owner and pointee types. Passing tests or
the breadth of landed code do not complete the boundary.
Canonical inheritance classification and inherited operator traversal consume
published TypeId class-base schemas. Completed-class publication also records the
finalized ABI virtual-base table as an ordered per-class `TypeId` schema; the
list position is the runtime table index and `findVirtualBaseIndex(source,
target)` is the query contract. Publication is transactional and rejects
conflicting order, duplicate targets, and self edges. AstToIr's derived-to-base
address and pointer adjustments, explicit static_cast lowering, and IRConverter
adjustment/validation now take virtual-base table indices from
`tryFindPublishedVirtualBaseIndex`, so all runtime virtual adjustments consume
the published TypeId facts and fail closed when a virtual conversion has no
published entry. `DerivedBaseConversionInfo` now carries only the relation kind:
neither a legacy virtual index nor a legacy offset remains in the conversion
plan. Runtime and constructor derived-to-base offsets both use
`tryFindPublishedDerivedBaseOffset`; the compatibility fallback is centralized
behind that query for specialization bases the canonical importer cannot yet
represent. Binary and range call selection no longer store AST owner
TypeIndexes: the published `BinaryOperatorCallTarget.declaring_type` is the
authority, and codegen derives its ABI owner handle at that boundary.
Concrete integral, bool, enum, and named bit-field class NTTPs share converted
canonical type/value identity in syntax and declaration imports, including nested
members, nonvirtual bases, equivalent named/braced values, and published base offsets.
Bit-fields normalize through the declared canonical type and published width, so signed
fields sign-extend, unsigned fields truncate, and bool stays 0/1. Enum fields retain
their declared enum TypeId and normalize through the published underlying layout.
Unnamed and zero-width bit-fields stay declaration/layout padding and add no value
subobjects. Constant-byte materialization extracts a bit-field from its declared storage
offset and width before scalar conversion, so static/global initializers agree with the
aggregate path. `tests/test_canonical_structural_nttp_bitfields_ret0.cpp` and the canonical
adapter scalar probe pin signed/unsigned/bool/enum conversion, padding, and nonzero base
offsets. Array members are ordered canonical array values keyed by their published element
type and outer bound; `StructuralArrayValue` keeps the published array TypeId as its child
and fails closed on bound, element-kind, or count mismatches. Nested arrays and record
elements recurse through the same value worklist, and element values are converted to the
declared element type before identity. Short array initializers zero-fill omitted elements
through the spec-aware materializer before structural value construction.
`tests/test_canonical_structural_nttp_arrays_ret0.cpp` covers nested arrays, record, bool,
enum, and scalar elements, equivalent named/braced values, and nonzero base offsets. Class
values retain ordered base/member values with an explicit base count;
import and tracing use worklists. Retain the 4,096-level import/trace guard on a
1 MiB stack; existing evaluator recursion is separate. Dependent and unmigrated
value shapes retain opaque expression identity.
Union groups publish variant/start/end flags in the existing member schema.
`StructuralUnionValue` keys an explicit active variant or no-active state by
record TypeId and group ordinal; live constexpr import remains deferred until
its payload preserves active-member identity. Retain the 4,096-level union-value
trace guard on a 1 MiB stack.
`StructTypeInfo::isAggregate` now excludes
classes with any reachable virtual base, not only direct virtual bases, so an
indirectly virtual diamond follows its most-derived constructor and installs the
completed-object virtual-base tables; `tests/test_indirect_virtual_base_aggregate_ret0.cpp`
covers the trait and the pointer conversion. Member access and member calls
through a pointer or reference to an intermediate base now apply the same
runtime virtual-base adjustment before using a member offset. Member calls on
complete object values use the finalized static base offset instead, so
implicit-template specializations without a hidden table work without a runtime
lookup. `tests/test_virtual_base_intermediate_member_access_ret0.cpp` covers
pointers, references, complete-object calls, and a non-virtual control.
Selected binary calls, including
range comparisons, consume context-owned call symbols and resolved descriptor IDs.
Declaring TypeId remains identity; owner TypeIndex remains only an ABI bridge
for object adjustment, with the deletion prerequisites below.
Unary arithmetic `+`, `-`, and `~` share selected member/free targets across
typing, constant evaluation, and lowering, including inherited receiver adjustment.
Selected unary targets retain lexical access/deletion validation; member calls
and symbols preserve ref qualifiers. Complete-class body queries receive base
graphs before typing; constructor-dependent object facts publish after resolution.
Named derived aggregates initialize bases and members through the shared emitter;
array decay retains local identity. Builtin dereference receivers preserve the
pointee address before inherited object adjustment.

Structural NTTP values now carry explicit union activity. Aggregate,
designated, default, and constructor initialization plus member assignment mark
the selected variant, structural equality includes the ordered selection, and
the canonical importer consumes published union-group schemas through
`structuralUnionValue`. The reduced union specialization case is protected by
`tests/test_canonical_structural_nttp_union_active_ret0.cpp`.

Float and double structural fields now intern as exact typed canonical values,
including array elements, so equivalent floating members share specialization
identity. Long double remains fail-closed until the value carrier stores its
full representation.

Null object, function, and member pointer fields now intern as typed canonical
null values. Non-null pointer targets still lack declaration identity and fail
closed.

Null pointer-like array elements also carry explicit canonical null values and
their declared element pointer depth. Non-null pointer array elements still
fail closed.

Class-template structural values now import for plain implicit specializations,
including substituted member values, while specializations with bases or union
groups remain fail-closed.

Direct and inherited non-virtual member-object pointer targets now use the
declaring class TypeId plus member ordinal as canonical identity; ambiguous and
virtual-base targets remain fail-closed. Direct and inherited member-function
pointer targets, including virtual functions, use the declaring class TypeId plus
declaration ordinal, with overloads selected by canonical signature.
Member-object-pointer and member-function-pointer arrays preserve their array
wrapper and element targets.

The architecture the remaining work builds on:

- `TypeSpecifierNode` carries an outermost-to-innermost `DeclaratorComponent`
  spine for named and abstract pointer/array/function/member-pointer declarators.
  The legacy flat pointer/array fields are projections retained only for
  unmigrated consumers.
- `CanonicalTypeTable` (owned by `FrontendContext`, single-mutex) interns and
  substitutes structural types keyed by `TypeId`, and publishes by `EntityId` or
  `TypeId`: record layout, record member/base/field schemas, class-base schemas,
  named type members, enum layout (underlying type and fixed/unfixed promotion),
  and record property facts. `TypeId` is the external identity; `StringHandle` is
  spelling only.
- `TypeContext` stores canonical descriptors in a pinned chunked arena, so a
  `CanonicalTypeDesc` reference returned by `get` stays valid while later
  descriptors are interned; `CanonicalTypeId` remains the identity. It reports the
  arena's used and reserved bytes into semantic arena accounting
  (`SemanticArenaComponent::Descriptors`), alongside the canonical node table's
  `Types` and the declaration builder's `Declarations`.
- `canonicalizeType` publishes `structural_type_id` for a projectable pointer or
  array when the canonical importer can represent that shape. `TypeContext::intern`
  does the same for pointer and array descriptors built outside `canonicalizeType`,
  after copying off any `get()` reference, and re-derives the id from the
  descriptor's current flat projection so a stale id cannot survive a projection
  change. Conditional array decay, conditional
  pointer selection, built-in subscript element extraction, and builtin unary
  `*` and `&` read that id and do
  not import while a descriptor reference is
  live; conditional array decay and built-in subscript element extraction are
  structural-only and fail closed on an array/pointer descriptor without published
  identity. Builtin unary `*` and `&` do the same for an array object. They
  import a local copy first, and a nested class of an instantiation still
  peels one flat pointer level. Flat pointer-level and
  array-dimension fields remain for shapes the
  importer cannot represent, and as a compatibility projection for unmigrated
  semantic writers (`PublishedPointerArray`). Materialization now exports every
  published pointer/array shape from its canonical `TypeId`, including unknown
  bounds and pointer qualifiers, rather than reading those flat fields. The shared
  `applyCanonicalDeclaratorBase` also recovers builtin and nominal bases and
  their cv from canonical identity; static-member lookup uses the same base bridge.
  Callable aliases therefore export their actual return base rather than treating
  the legacy `FunctionPointer` category as that base. The marker
  still makes `TypeContext::intern` refresh identity after a flat writer changes
  its projection. Native fault-injection coverage verifies that stale flat fields
  cannot change a materialized query, while re-interning refreshes the identity.
  Native base-index publication also exposed a member-CV writer bug hidden by an
  incomplete-constructor-argument fallback: a const receiver now qualifies the
  outermost pointer member (or an array's pointer element), rather than its
  pointee. Declared top-level member CV is retained. Full declared pointee and
  intermediate pointer qualifiers still require the member-TypeId migration;
  the legacy metadata gap is recorded in `KNOWN_ISSUES.md`.
  The canonical array shaper
  emits the unknown-bound wrapper outside the known dimensions, so a projectable
  `T(*)[][N]` publishes `Pointer -> Array(?) -> Array(N) -> T` (and an ordinary
  `T[][N]` publishes `Array(?) -> Array(N) -> T`) instead of dropping the `[?]`
  bound. The nominal array importer now shares the builtin array shaping, so a
  `Record(*)[][N]`/`Enum(*)[][N]` element also publishes the same identity rather
  than staying flat. Template argument deduction for a pointer-to-array
  parameter consumes the decayed argument dimension before matching the declared
  pointee bounds, so `template<class T> void f(T (*)[N])` binds `T` to the element
  type instead of the remaining subarray. Substitution then publishes one
  pointee array bound and accepts the matching call argument; a mismatched
  pointee bound is rejected during deduction. A materialized descriptor and the ordered-declarator import
  recover a class-template specialization base from its type index through
  `importCanonicalNominalComponent`, so an array whose element is a
  specialization also publishes structural identity. Built-in subscripting on an
  interleaved pointer/array object now resolves each selected step from the
  sema-published canonical result type instead of flattening all array bounds
  into one row-major shape. An array of pointers followed by a pointer-to-array
  therefore uses the pointer stride for the array step and the row stride for the
  pointer-to-array step;
  `tests/test_interleaved_pointer_array_subscript_identity_ret42.cpp` pins the
  address and element read. Global and local interleaved array objects with a
  known outermost bound now own inline storage with pointer-sized elements and
  lower identifiers as array addresses, covered by
  `tests/test_interleaved_pointer_array_object_storage_ret42.cpp`. Brace
  initialization of those objects stores one outermost element per clause, and
  a short list value-initializes the rest. An extra clause is
  `TooManyInitializers` (1510).
  `tests/test_interleaved_pointer_array_initializer_ret0.cpp` covers global and
  local `int (*a[N])[4]` lists, including a null tail. An array of unknown
  bound is completed from that same list: n > 0 clauses define an array of n
  elements, so `int (*a[])[4]` becomes `int (*a[n])[4]`. A trailing comma does
  not add an element, and an empty list is
  `EmptyUnknownBoundArrayInitializer` (1513).
  `tests/test_interleaved_pointer_array_unknown_bound_ret0.cpp` covers global
  and local copy-list and direct-list initialization, including a null clause.
  Range-for over a known outermost bound visits one outermost element, so
  `int (*cells[N])[4]` yields `int (*)[4]`, including a bound completed from
  the brace list. The hidden iterator is that array decayed to a pointer to
  the element, `int (*(*)[4])`. `tests/test_interleaved_pointer_array_range_for_ret0.cpp`
  covers a value, a reference, and `auto`, on a global and a local array.
  The bound helper and the range-for helper are not recursive. Brace lists
  still use the existing `parse_brace_initializer` recursion for nested
  clauses only. No stack reserve was changed. The sharded build is `/Od`, so
  there is no `-fstack-usage` number.
  `SemanticAnalysis::pointerToArrayShape` is the single
  codegen resolver for a pointer whose immediate pointee is an array; the
  dereference/array-to-pointer-decay, multi-dimensional pointer-to-array
  subscript, and the identifier- and member-rooted multi-dimensional array
  collectors read it (the member-rooted one through
  `memberPointerToArrayShape`, which interns the member descriptor first) and
  fail closed when a pointer-to-array descriptor has no published structural
  identity. The member-access array-dimension helpers read
  `CanonicalTypeTable::arrayDimensions`, the single outer-to-inner array view
  (unknown bound as zero), so codegen no longer reads any flat
  `pointee_array_declarator`/`has_unsized_outer_array_dimension` field. The
  pointer resolver keeps one structural walker: it projects
  `exportCanonicalDeclarator`, the canonical declarator exporter, instead of
  re-walking node kinds. `PointerToArrayShape` is a transitional projection with
  a named deletion boundary — it is removed when lowering consumes sema-produced
  typed lowering records (architecture boundary 9) and no longer derives shape.
  Exit criteria 4 and 5 are not closed: semantic-internal descriptor
  construction and equality still read the flat projection. Conditional array
  decay does not. `decayArrayForConditional` takes `CanonicalTypeTable::pointer`
  of the array element and re-exports that pointer through
  `materializeTypeSpecifier`, so interning cannot republish the operand's flat
  dimensions over the decayed identity. The in-place flat dimension rewrite is
  gone. Builtin unary `*` and `&` take the canonical pointee, or
  `CanonicalTypeTable::pointer` of the undecayed operand, when that operand
  has a published id or its local copy imports. `*` of an array removes one
  outermost bound, which is `[conv.array]` followed by indirection, and array
  cv stays on the element. `&` of an array lvalue is a pointer to that array.
  A projectable result is re-exported through `materializeTypeSpecifier`.
  An interleaved pointer/array has no legacy projection, so the result keeps
  its structural id and does not set `PublishedPointerArray`. An implicit
  class-template specialization records the primary `TemplateDeclId` and its
  published arguments, so `*this` there is that specialization. A nested class
  of an instantiation has no per-specialization entity yet, and a dependent
  class-template pattern is not a concrete specialization, so those pointers
  still pop one flat pointer level. An array object without a
  published id fails closed. User-defined `operator*` and `operator&` stay on
  their resolved-function paths.   Builtin `->` uses that same canonical `*`
  pointee and re-exports it, including that flat peel; the dot path is unchanged.
  Builtin `new` and `new[]` take `CanonicalTypeTable::pointer` of the allocated
  type, or of its initial element when that object is an array, and re-export
  it. An array typedef decays one outermost bound. The runtime `new T[n]` bound
  is not stored in the type node, so it is not peeled again. A projectable
  pointer or array without a published id fails closed. An unmigrated
  non-projectable type still pushes one flat pointer level. This slice does not
  publish nested class entities, so nested-instantiation and dependent-pattern
  pointers are unchanged. Parser typing of a new-expression follows the same
  `[expr.new]` result, and a ptr-operator in the new-type-id belongs to the
  allocated type.
  The 3A rollup remains 10/79.
- The shared type-trait evaluator classifies the structural `[meta.unary.prop]`
  family plus class/union/qualification and published record properties from
  canonical identity; the folded, constexpr, lazy-constraint, and lowered paths
  route through it. Zero-argument constructibility variants answer from published
  record facts.
- A canonical-table probe (`Canonical identity is independent of interning and
  string order`) pins that a repeated structural request returns one `TypeId`
  regardless of unrelated interning and string-table insertion order, covering
  the interning half of exit criteria 2 and 6; parse-order coverage across two
  parses remains open.
- Parser-side overload ranking and semantic conversions use the structural
  planner and canonical base graph for builtin arithmetic, array decay,
  null-pointer, function decay, object-pointer, member-pointer reference
  temporaries, direct derived-to-base reference and pointer conversions, regular
  function-pointer and same-owner member-function-pointer pairs,
  conversion-function member-pointer-to-`bool` tails, public nominal
  derived-to-base tails (including template-specialization bases when their
  class-base schemas are available), conversion-operator results bound to
  const lvalue references and rvalue references, and the member-pointer owner graph.
  Selected conversion-operator access and base-tail checks share
  `checkSelectedConversionOperatorAccess`; call-argument reference bindings and
  brace reference initialization apply it, covered by
  `test_private_conversion_reference_*_e1617.cpp`. A reference-returning
  conversion operator now uses reference-return call ABI, and its
  derived-to-base tail adjusts the converted lvalue address instead of
  materializing a temporary from the result value. Copy-initialization and call
  arguments for a nonzero base offset are covered by
  `test_conversion_operator_const_reference_base_tail_ret0.cpp`.
  Named function expressions retain their function type until reference binding
  or value decay is selected. Call lowering preserves the pointer-sized address
  for function-to-pointer decay, including callable aliases with a function base.
  Structured callable components retain dependent alias declaration and argument
  identities. Chained aliases keep their use-site arguments through substitution;
  substituted nominal arguments bind their published entities before import.
  A retained component `TypeId` remains authoritative over its flat projection.
  Concrete materialization supplies callable signatures, member-pointer owners,
  and scalar projections before lowering.
  Once a stored type argument resolves, later alias-argument passes preserve its
  nominal identity even if its spelling matches another template parameter.
  Keep the reduced record/enum collision and default-NTTP trait regressions.
  A namespace alias whose target is a dependent member alias now keeps the
  concrete member alias as the semantic source instead of collapsing it to the
  terminal nominal `TypeInfo`, so an instantiated `using type = T` retains the
  pointer or reference shape carried by `T`.
- `decltype` implements the `[dcl.type.decltype]` value-category rule for
  parenthesized id-expressions, the last comma operand, dereference, member
  access, built-in subscripting, and callable-object calls.
- Class-template substitution, alias-template forwarding, and member-pointer
  owner identity preserve canonical `TypeId`s across AST copies and substitution.
  An ordered member-function-pointer declarator whose callable return carries its
  own declarator (for example `int* (Owner::*)() const`) imports structurally:
  the spine's return components fold into the canonical function signature and
  the owner `TypeId` is preserved. A member-function-pointer spine whose callable
  is represented only by the member component suppresses base-level signature
  import before applying that component, so the signature is not folded twice.
  MSVC and Itanium mangling fold the same spine components into the callable's
  return type instead of rejecting the form, and IR lowers the value as a
  fixed-size scalar through the declaration, parameter, return, and identifier
  paths. A template-parameter owner (`int* (T::*)() const`) is published as that
  class's canonical identity during substitution, before a dependent `noexcept`
  operand is canonicalized. An opaque scoped enum with a fixed underlying type
  keeps its complete forward-declaration layout with explicit
  `ForwardDeclaration` and `FixedUnderlying` flags; the native suite asserts that
  contract alongside by-EntityId pointer and reference import.
  An alias template whose target is a parenthesized function declarator
  (`template <class T> using Fn = int (T);`) now parses through the shared
  `parse_type_alias_function_type` and its instantiated structured signature is
  substituted through `substituteTemplateFunctionSignatureTypes` where the alias
  target is rebuilt, so `Fn<int>*` pointer, parameter, and overload uses resolve
  and mangle concretely. `tests/test_alias_template_function_type_target_ret0.cpp`
  covers function and function-pointer alias targets. A declaration whose type is
  a function-type alias (`using F = int(int); F declared;`) now declares a
  function: `parse_function_declaration_with_storage_specs` synthesizes the
  return type and parameter nodes from the alias's structured signature when there
  is no parameter list, so the declaration is no longer misregistered as a global
  variable and `tests/test_function_type_alias_declaration_ret0.cpp` no longer
  crashes.

Compatibility counters and identity inventories are baselined under
`tests/migration_counters/`; the flat classifier in `TypeTraitEvaluator.cpp`
remains only for the families listed under remaining work item 2.

Boundary **2F** is complete. The archived legacy negative reproducers under
`tests/unsupported_boundary_2f/` have all been recovered as encoded `_e<ID>`
negative tests and the archive index is empty. The final reproducer, an
out-of-line static member function of a class template that references a
non-static data member, is rejected with `NonStaticMemberUseInStaticFunction`:
a replayed out-of-line member body now surfaces its structured rejection instead
of being masked by a generic `OutOfLineMemberNotAttached` attachment failure.

## Next work

Continue boundary 3A in this order.

0. **Declarator consolidation: complete.**

1. **Make `TypeId` the conversion currency (in progress).** Finish replacing
   syntax-facing conversion ranking with structural planning. Put remaining flat
   descriptor reads behind compatibility boundaries that preserve callable identity,
   nested cv, array decay, and value category.

   **Remaining:** remove compatibility selection where canonical import and declaration
   publication cover the source; implement substitution-aware ranking for unsupported
   callable conversions and user-defined constructor paths; complete derived-to-base
   and callable-component tails. Keep compatibility for shapes the canonical importer
   or declaration bridge cannot yet represent.

   **Active-union structural identity: complete for declared groups.**
   `EvalResult` now carries the selected variant explicitly on the member
   binding, and `StructuralClassValue` stores ordered `union_selections`
   (`first_variant`, active declaration ordinal, or explicit no-active).
   Aggregate, designated, default, and constructor initialization plus member
   assignment maintain that state; structural equality and hashing include it;
   rehydration restores it; and the canonical importer consumes the published
   union-group schema and calls `structuralUnionValue` for struct anonymous
   groups and named union records. Activity is never inferred from scalar bits
   or default-filled variant bindings. `tests/test_canonical_structural_nttp_union_active_ret0.cpp`
   protects `Key{.first=0}` versus `Key{.second=0}`, named/braced equivalence,
   a named union member at a nonzero offset, and union-valued base
   specializations. The 3A rollup remains 10/79; no exit criterion is closed.

   **Float/double structural value import: complete for declared fields.**
   `CanonicalTypeTable::floatingValue` interns an exact typed Float/Double bit
   pattern, and structural import now dispatches floating record and array
   element fields to it after validating the declared width. Float and double
   fields therefore share class/type/value identity between syntax and
   declaration imports without host-string or rounding identity. Long double
   fails closed because the evaluator still carries only a 64-bit
   representation. `tests/test_canonical_structural_nttp_float_ret0.cpp`
   protects equivalent and changed float, double, and float-array members plus
   a base specialization, and the native canonical suite includes
   a mutation-checked typed importer test. The 3A rollup remains 10/79; no exit
   criterion is closed.

   **Null pointer-like structural value import: complete.** Null object,
   function, and member pointer fields now intern as typed canonical
   `NullPointerValue` nodes keyed by their declared pointer-like type. The
   evaluator marks an explicit null member value, rehydration recovers the
   declared pointer type from the published member schema, and the importer
   accepts only canonical Pointer/MemberObjectPointer/MemberFunctionPointer
   field types. Non-null pointer targets still lack declaration identity and
   fail closed. `tests/test_canonical_structural_nttp_null_pointer_ret0.cpp`
   protects null object and member pointer fields, equivalent braced values,
   changed sibling members, and a base specialization; the native canonical
   suite includes a mutation-checked importer test. The 3A rollup remains
   10/79; no exit criterion is closed.

   **Null pointer-like array elements: complete.** Structural array payloads
   now record the declared element pointer depth, mark null pointer-like
   elements explicitly, and rehydrate their declared pointer type. The canonical
   importer accepts Pointer/MemberObjectPointer/MemberFunctionPointer array
   element types and interns null `NullPointerValue` elements. Non-null pointer
   elements still fail closed, and pointer depth participates in structural
   equality/hash so pointer-array shapes do not merge.
   `tests/test_canonical_structural_nttp_null_pointer_array_ret0.cpp` protects
   equivalent null object pointer arrays, changed sibling members, and a base
   specialization; the native canonical suite includes a mutation-checked
   importer test. The 3A rollup remains 10/79; no exit criterion is closed.

   **Class-template structural value import: complete for plain implicit
   specializations.** The structural importer now imports an implicit class
   template specialization through `importCanonicalClassTypeInfo` instead of a
   bare TypeIndex syntax node, and `structuralValue` accepts a
   TemplateSpecialization class TypeId. A `Key<int>`-style value therefore
   keeps its specialization identity while its substituted member values remain
   typed canonical subobjects. Specializations with bases or union groups still
   fail closed. `tests/test_canonical_structural_nttp_class_template_ret0.cpp`
   protects equivalent and changed `Key<int>` values plus a base specialization;
   the native canonical suite includes a mutation-checked importer test and the
   canonical table accepts specialization children directly. The 3A rollup
   remains 10/79; no exit criterion is closed.

   **Non-null direct member-object pointer values: complete.** Structural
   member-object-pointer fields now resolve the selected direct data member
   against the declared owner's published record field schema and intern a
   canonical `MemberObjectPointerValue` carrying the member ordinal. The owner
   comes from the canonical member-pointer type rather than a spelling key, and
   inherited targets, bit-fields, member-function pointers, and specialization
   owners without published ordinals fail closed.
   `tests/test_canonical_structural_nttp_member_object_pointer_ret0.cpp`
   protects equivalent and changed direct member targets plus a base
   specialization; the native canonical suite includes a mutation-checked
   importer test and ordinal-validation guards. The 3A rollup remains 10/79; no
   exit criterion is closed.

   **Non-null direct member-function pointer values: complete for unique
   non-virtual members.** Structural member-function-pointer fields now resolve
   a unique, direct, non-virtual member function in the declared owner and
   intern a canonical `MemberFunctionPointerValue` carrying its declaration
   ordinal. Overloaded names, virtual functions, constructors, destructors,
   inherited targets, and unpublished specialization owners fail closed.
   `tests/test_canonical_structural_nttp_member_function_pointer_ret0.cpp`
   protects equivalent and changed direct member targets plus a base
   specialization; the native canonical suite includes a mutation-checked
   importer test and canonical ordinal guards. The 3A rollup remains 10/79; no
   exit criterion is closed.

   **Inherited member-object pointer values: complete for unambiguous
   non-virtual bases.** `MemberObjectPointerValue` now packs the declaring class
   `TypeId` with the member ordinal, and the adapter searches direct members
   first, then unambiguous non-virtual bases with a visited worklist. Ambiguous,
   virtual, bit-field, and unpublished-base targets fail closed. This keeps
   `&Object::base_member` distinct from a same-ordinal direct member while
   preserving the declared member-pointer type identity.
   `tests/test_canonical_structural_nttp_inherited_member_object_pointer_ret0.cpp`
   protects an inherited target, a changed member in the same base, and a
   same-ordinal direct member; the native canonical suite includes a
   mutation-checked importer test. The 3A rollup remains 10/79; no exit criterion
   is closed.

   **Overloaded member-function pointer values: complete for direct non-virtual
   overloads.** Candidate member functions are filtered by name and rebuilt into
   canonical function signatures from their declaration return type, adjusted
   parameter nodes, calling convention, cv/ref qualifiers, variadic flag, and
   noexcept state. Exactly one signature must match the declared member-pointer
   type before its declaration ordinal is interned. Virtual, constructor,
   destructor, inherited-overload, and ambiguous signature cases fail closed.
   `tests/test_canonical_structural_nttp_overloaded_member_function_pointer_ret0.cpp`
   protects overload selection by signature, a changed member name, and
   equivalent braced values; the native canonical suite checks the selected
   ordinals. The 3A rollup remains 10/79; no exit criterion is closed.

   **Member object pointer array elements: complete.** Flat member-pointer
   member declarations now restore their layout-supplied outer array wrappers,
   so the published member type is `Array(MemberObjectPointer(...))` rather than
   a scalar member pointer. Structural array elements preserve the selected
   member name, rehydrate the member-pointer marker, and import through the
   existing declaring-class/ordinal path. Null elements keep the null-pointer
   path; non-null object/function pointer elements still fail closed.
   `tests/test_canonical_structural_nttp_member_pointer_array_ret0.cpp` protects
   changed member-pointer array elements and equivalent braced values; the
   native canonical suite inspects the array’s declaring class and ordinals.
   The 3A rollup remains 10/79; no exit criterion is closed.

   **Member function pointer array elements: complete.** Member-pointer
   declarators whose outer array suffix lived only in the ordered spine are now
   classified as inline array storage during struct layout. The published member
   type is `Array(MemberFunctionPointer(...))`, and structural array elements
   import through the same member-function-target resolution as scalar fields,
   including overload selection by signature. Null elements keep the null path;
   object/function pointer elements still fail closed.
   `tests/test_canonical_structural_nttp_member_function_pointer_array_ret0.cpp`
   protects changed member-function-pointer array elements and equivalent braced
   values; the native canonical suite inspects the array child and selected
   ordinals. The 3A rollup remains 10/79; no exit criterion is closed.

   **Inherited member-function pointer values: complete for unambiguous
   non-virtual bases.** `MemberFunctionPointerValue` now packs the declaring
   class `TypeId` with the function ordinal. The adapter searches direct member
   functions first (name hiding), then unambiguous non-virtual bases with a
   visited worklist, selecting overloads by canonical signature. Ambiguous
   base-name matches, virtual targets, constructors, destructors, and
   unpublished base schemas fail closed.
   `tests/test_canonical_structural_nttp_inherited_member_function_pointer_ret0.cpp`
   protects an inherited target, a changed member in the same base, and a
   same-ordinal direct member; the native canonical suite checks declaring
   classes and ordinals. The 3A rollup remains 10/79; no exit criterion is
   closed.

   **Virtual member-function pointer values: complete for structural
   identity.** Direct and inherited virtual member functions now use the same
   declaring-class TypeId plus declaration-ordinal identity as non-virtual
   targets. Overload selection remains by canonical signature, and name hiding,
   ambiguity, constructors, destructors, and unsupported base schemas still fail
   closed. Runtime lowering of virtual member pointers remains a separate
   boundary-9 gap; this slice only completes their structural NTTP identity.
   `tests/test_canonical_structural_nttp_virtual_member_function_pointer_ret0.cpp`
   protects direct/inherited virtual targets and changed members; the native
   canonical suite checks declaring classes and ordinals. The 3A rollup remains
   10/79; no exit criterion is closed.

   **Next slice: import remaining non-null pointer and specialization facts.**
   Non-null object/function pointer targets, ambiguous/virtual-base member
   pointers, specialization base/union facts, and the nested
   anonymous-union-in-anonymous-union hierarchy still need canonical subobject
   identity. Extend the typed-value importer and its specialization base facts
   before deleting `legacyDerivedBaseOffset`; audit dependent and other
   non-integral specialization roots too. Keep the worklist guard and the
   nonzero-offset guards in
   `test_canonical_structural_nttp_{base_source,equivalence,subobjects,bitfields,arrays,union_active,float,null_pointer,null_pointer_array,class_template,member_object_pointer,member_function_pointer,inherited_member_object_pointer,overloaded_member_function_pointer,member_pointer_array,member_function_pointer_array,inherited_member_function_pointer,virtual_member_function_pointer}_ret0.cpp`.
   See `KNOWN_ISSUES.md`.

   **Owner-index deletion gates:** virtual-base ABI facts are published by class
   TypeIds and consumed by AstToIr, explicit-cast, IRConverter, and constructor
   adjustments. Nonvirtual offsets prefer canonical graph offsets, binary/range
   AST owner-index fields are deleted, and the plan struct retains only the
   relation kind. The remaining TypeIndex uses are explicit ABI bridges plus the
   centralized importer-fallback walk. Delete that walk after the importer covers
   the remaining specialization bases. Retain graph-authority, metadata copy/reset
   and nonzero/virtual-base regression guards. Conversion-function declaration
   schemas and unsupported importer shapes remain separate. Reference/value-tail
   lowering gaps are in `KNOWN_ISSUES.md`.

   **Unary compatibility deletion:** audit no-context and unimportable operand
   roots before removing `findUnaryOperatorOverloadCompatibility`. Also migrate
   the separate compatibility candidate gathering in the mixed member/free
   selector; deleting the named helper alone does not remove that dependency.

2. **Migrate remaining flat consumers (independent parallel track).**
   1. **The constructibility family.** The zero-argument variants and published
      record facts are canonical; the argument-bearing record query still reads
      `StructTypeInfo` through constructor overload resolution. Default member
      initializers now contribute their selected constructor's exception
      specification (scalar, single- and multi-dimension array, implicit or
      defaulted copy/move, constant `noexcept(expr)`, a class-template
      constructor whose `noexcept` operand is dependent and is re-evaluated per
      specialization, and an array element that default-constructs the member
      type from a prvalue `T{}` or an empty brace `{}` under guaranteed copy
      elision). Every function-like parse path now folds a constant `noexcept`
      operand and rejects a non-constant one with `NoexceptSpecifierNotConstant`
      (#1021) through one `resolveParsedNoexcept` / `applyParsedNoexcept` choke
      point, including static member functions, whose specifier was previously
      dropped, and destructors: a non-constant destructor operand is rejected
      with #1021, an explicit specifier is marked on the node, and a
      class-template destructor's retained dependent operand is re-evaluated at
      instantiation. Class-template member-function instantiation re-evaluates a
      retained dependent operand, and both `noexcept` evaluators now share the
      `tryResolveInstantiatedMemberNoexcept` member-call resolution through the
      receiver's type (`CallNodeHelpers.h`), so the member-function
      keyword-present interim answer is gone, and codegen `noexcept` lowering now
      delegates to the single `ConstExpr::Evaluator::is_expression_noexcept`
      authority; its own `AstToIr::isExpressionNoexcept` evaluator is deleted.
      `noexcept(expr)` is a compile-time constant, so the terminal fix is to fold
      it once in expression sema (boundary 4) and have lowering read the value.
      Remaining: an overloaded binary operator and a user-defined conversion
      function now consult their selected declarations, but a qualified
      static-member call is not yet resolved: `parse_primary_expression`'s
      qualified path (`lookup_qualified` returns null for `S::f`) builds a
      free-function callee, and resolving the static member there was reverted
      because sema then throws "Resolved member function access has no semantic
      owner class" — the same owner-class identity gap as the boundary-4
      member-function-pointer handoff (an earlier `parse_postfix_expression`
      resolver retry was likewise reverted), and the pseudo-destructor
      `noexcept` consumer resolves the
      instantiated destructor for cast, identifier, arrow, and dereference object
      forms but still falls back to the pattern for an object expression it cannot
      type; a class constructor call now consults the record's default
      constructor (zero-argument) or the unique user-provided same-arity
      constructor (argument-bearing), while an ambiguous selection stays
      conservative; the canonical argument-bearing query uses the class's
      published constructor schema to answer exact-match viability and selected
      user-provided constructor exception facts. The schema is keyed by canonical
      `TypeId` and published from the declaration node for both records and
      class-template specializations (no `StructTypeInfo` at publication).
      Fixed-arity candidates with supported canonical standard conversions use
      per-argument conversion ranks. These include builtin conversions,
      supported pointer conversions, same-owner member-pointer conversions, and
      array/function decay. Single-argument builtin-to-builtin scalar
      construction also uses the canonical planner; supported pointer and
      member-pointer scalar conversions use it as well, and `nullptr_t` to
      `bool` is handled as direct initialization. Single-argument reference
      targets use the canonical reference-binding planner for supported
      bindings and temporary materialization. Record constructor reference
      parameters with the same canonical referent or a public canonical base
      use direct reference ranking, while differing builtin scalar referents
      use promotion or conversion ranking for temporary binding to const
      lvalue references and rvalue references. The shared TypeId reference planner
      also handles published enum arithmetic, pointer/member-pointer boolean and
      null conversions, array/function decay, and supported callable noexcept
      relaxation. Similar pointer referents use structural reference compatibility
      for direct qualification binding, preserving pointer-object alias identity
      and rejecting related lvalues bound to rvalue references. Identical-referent direct-binding
      decisions remain authoritative, including rejection of mutable lvalue
      references to conversion temporaries; nullptr-to-bool reference arguments
      remain rejected. The shared TypeId-only planner replaces the local
      same-referent helper; canonical-only native tests exercise its ranks and
      the constructor probe and trait query without TypeInfo identities, and
      `test_canonical_constructor_base_reference_args_ret0.cpp` covers record
      and specialization bindings, rejection cases, overload ranking, and a
      lazy concept. `test_canonical_scalar_reference_temporary_args_ret0.cpp`
      covers builtin scalar lvalue/rvalue reference arguments, promotion versus
      conversion ranking, selected constructor exception facts, and a lazy
      concept. Class user-defined conversions, missing enum facts, nominal
      pointer/member-owner adjustments, and unsupported dependent/callable
      relations still defer to compatibility resolution. Expression sema consumes
      the same TypeId reference plan before entering its residual compatibility
      path, and its binding descriptor retains the canonical parameter TypeId for
      lowering. Direct declaration initialization, constructor expressions,
      and initializer-list constructor arguments now consume sema reference
      binding plans through the shared call-argument materializer. The new
      `test_scalar_reference_ctor_temp_materialization_ret0.cpp` regression
      covers converted references, direct alias identity, prvalues, templates,
      and mixed reference/value parameters. The new
      `test_constructor_template_const_lvalue_ref_ret0.cpp` protects lazy
      constructor-template materialization from dropping declared cv
      qualifiers while combining them with deduced template types.
      Copy-initialization and new-expressions now use the same semantic
      reference-binding plans for converted scalar arguments; the separate
      `test_scalar_reference_ctor_temp_copy_init_ret0.cpp` and
      `test_scalar_reference_ctor_temp_new_ret0.cpp` regressions protect those
      paths. Member, direct/virtual base, and delegating initializer arguments
      also carry semantic binding plans into constructor-call lowering. The
      `test_scalar_reference_ctor_temp_member_init_ret0.cpp`,
      `test_scalar_reference_ctor_temp_base_init_ret0.cpp`, and
      `test_scalar_reference_ctor_temp_delegating_init_ret0.cpp` regressions
      protect converted reference arguments on those paths. Separate
      `test_scalar_reference_ctor_temp_virtual_base_ret0.cpp` and
      `test_scalar_reference_ctor_temp_aggregate_base_forward_ret0.cpp`
      regressions cover virtual-base binding and aggregate-base forwarding.
      Constructor normalization now prefers the declaration's recorded owner,
      reads published member/base TypeIds when available, and confines legacy
      declaration lookup to an explicit canonical bridge. Initializer lowering
      consumes the selected semantic constructor with its bindings instead of
      selecting a second overload; new-expressions share the annotation helper.
      `test_ctor_initializer_selected_overload_ret0.cpp` protects promotion-ranked
      member/base/delegating overloads, and
      `test_scalar_reference_ctor_temp_owner_identity_ret0.cpp` protects owners
      sharing names across namespaces and class-template specializations.
      Class registration now distinguishes a declaration's registered name from
      an unqualified lookup alias, so a new global declaration cannot overwrite
      an existing namespace class; the reduced
      `test_record_lookup_alias_declaration_identity_ret0.cpp` protects that
      identity. Function designators retain function type until conversion, while
      explicit address-of retains pointer type; a conditional expression that
      selects between two function designators keeps that function identity, and
      an indirect call through a reference loads the pointer object only when the
      referent is a pointer rather than a function. The
      `test_conditional_function_designator_identity_ret0.cpp` and
      `test_function_pointer_reference_indirect_call_ret0.cpp` regressions
      protect those two paths. The
      `test_canonical_constructor_reference_conversion_temporaries_ret0.cpp`
      regression covers pointer/enum/array/function conversions, null pointer
      materialization, function references, and pointer-object aliasing.
      Native TypeId-only tests cover accepted, rejected, and deferred structural
      reference plans, constructor/scalar trait agreement, and 4,096 nested
      pointer qualifications under the normal OS stack limit. Constructor
      selection itself still reads declaration candidates through StructTypeInfo;
      these changes do not close another 3A exit criterion.
      Unscoped enum
      arguments use published promotion and underlying types for builtin
      scalar and constructor parameters; scoped enum-to-builtin conversions
      are rejected canonically. Implicit copy/move constructor schemas derive
      their self-reference parameter TypeIds from the owning canonical
      identity when synthesized parameter nodes lack entity bindings.
      Variadic candidates and other record
      conversion shapes use constructor overload resolution to read the
      selected user-provided constructor's exception specification. The schema
      records trailing default-argument arity, so canonical viability accepts
      those calls; nothrow queries with omitted arguments defer because the
      default expression's exception behavior is not published. The triviality
      variant reads the selected constructor (user-provided is non-trivial; an
      implicit/defaulted copy or move is trivial exactly when the class is
      trivially copyable), and an
      implicit/defaulted nothrow selection follows the copy or move construction
      through the subobjects; a call's receiver/argument evaluation,
      which is not counted because `is_expression_noexcept` is boolean and cannot
      separate a known-throwing operand from one it cannot resolve (a blanket
      argument check was tried and regressed template noexcept cases, so it needs
      a tri-state result). Triviality and lifetime traits now walk published
      TypeId member/base schemas using direct special-member facts; unsupported
      or incomplete schemas defer to the compatibility classifier.
   2. **Recover nominal identity only when the counter proves the flat projection lost it.**
      `gCanonicalTypeEntityImportRecoveryCount` found 11 recoveries in the fixed
      corpus, including two in the reduced
      `test_canonical_trait_operand_entity_ret0.cpp` regression. The normalized
      trait operand now preserves its source `EntityId` and injected declaration
      before canonical import; the fixed corpus reports zero recoveries after
      that change. No new identity accessor was needed.
   3. **Template argument and substitution storage.** The lazy constraint
      evaluator substitutes a template parameter by name against
      `template_param_names`; boundary 6 replaces that with depth-and-index
      parameters, then constexpr type queries and IR layout/subscript paths.
      Callable signature substitution now stamps published type-parameter
      declaration/index identities through nested signatures with an explicit
      worklist. When both the pattern and argument import canonically, the
      substituted `FunctionType` retains the complete canonical `TypeId` and
      re-exports supported pointer/array/reference declarators instead of storing
      a duplicate ordered spine. Function identity comparison permits canonical
      components to match legacy projections, compares TypeIds when both are
      present, and keeps hashes representation-neutral.
      Explicit source NTTP expression identity now survives argument
      reclassification and the `TemplateTypeArg`/`TemplateArgInfo` round trip,
      so declaration-based class specialization import emits the same opaque
      NonType Spec link as syntax-based import when that identity is present.
      Dependent defaults now stay dependent until substitution, and simple
      member-function-pointer defaults carry the declared class/signature into
      specialization lookup. Regressions cover dependent integral defaults,
      defaulted member-function pointers, and explicit specialization stamps.
      Dependent record arguments and dependent qualified-id substitution retain
      an existing source ExprId when rebuilding a concrete NTTP. Stored and
      dependent-record value-pack expansion now evaluate each concrete element
      separately, preserving its typed value identity and source stamp, or
      interning an expanded expression stamp when no source stamp exists. Focused
      unit coverage protects typed identity and expression-stamp reconstruction.
      Deferred dependent-base arguments now preserve the evaluated
      `NonTypeValueIdentity` and source `ExprId` when materialized. Ternary
      arguments retain function-address identity by evaluating the selected
      branch through the NTTP-aware path; nested ternaries are traversed
      iteratively. `test_deferred_base_nttp_function_pointer_identity_ret0.cpp`
      covers a dependent-base argument selecting an explicit function-pointer
      specialization through a conditional expression. Other NTTP reconstruction
      paths remain outside this slice. Dependent stored NTTP arguments now also
      keep their evaluated value identity and original `ExprId` during
      materialization, interning the substituted expression when no source stamp
      exists. `test_materialize_stored_nttp_function_pointer_identity_ret0.cpp`
      covers function-pointer specialization lookup through this path.
      Member-alias materialization now preserves typed identity when resolving
      a dependent static-member NTTP, and early initializer normalization keeps
      pointer-to-member expressions available for that resolution.
      `test_member_alias_static_memberptr_nttp_identity_ret0.cpp` protects the
      specialization lookup. This extends the NTTP follow-up without completing
      a 3A exit criterion; the 3A rollup remains 10/79.
      A non-projectable pointer/array type-id is stored with its ordered
      declarator and restored when the argument is materialized.
      `tests/test_template_arg_interleaved_pointer_array_ret0.cpp` checks
      `int (*[2])[4]` and a pointer to that array. A declarator written around
      a template parameter is kept on substitution: `Hold<T (*[2])[4]>` with
      `T = int` is `Hold<int (*[2])[4]>`, and `T*` around that array stays a
      pointer to the array object.
      `tests/test_template_pattern_interleaved_declarator_ret0.cpp` checks both
      with unevaluated `sizeof`. IR storage of the resulting member still
      fails closed. Non-projectable function/member-pointer shapes remain
      deferred until canonical substitution and export coverage is available.
   4. **Conditional array decay.** `decayArrayForConditional` no longer rewrites
      `array_dimensions`, `pointer_levels`, or `has_unsized_outer_array_dimension`
      to describe the decay. The result identity is the canonical pointer to the
      array element. `materializeTypeSpecifier` re-exports that pointer into the
      compatibility projection, because `TypeContext::intern` still refreshes a
      `PublishedPointerArray` descriptor from those fields. Descriptor
      construction and equality remain on the flat projection. This does not
      close a 3A exit criterion.
      `tests/test_conditional_multidim_array_decay_ret0.cpp` protects
      `[conv.array]` for `int[2][3]`, `int[][2]`, a const array, `long` and
      `Cell` rows, a `short` matrix, and a class-template array member. The old
      rewrite selected an element pointer or an array of pointers, so the row
      stride was wrong. The decay helper is not recursive. It runs after child
      expression inference returns and calls the existing iterative materializer
      once, so native stack use does not grow with array rank or nested
      conditional depth. No stack reserve was changed.
   5. **Builtin unary `*` and `&`.** Published pointer and array operands no
      longer pop `pointer_levels` or erase `array_dimensions`. The result
      identity is the canonical pointee, or `CanonicalTypeTable::pointer` of
      the undecayed operand. `[expr.unary.op]` and `[conv.array]` remove one
      outermost array on `*` and do not decay the operand of `&`. Cv on a
      pointer object does not qualify the pointee; cv on an array stays on the
      element. A projectable result is re-exported so intern's flat refresh
      cannot republish the operand. An interleaved result keeps its structural
      id because it has no legacy projection. An implicit class-template
      specialization records the primary `TemplateDeclId` and the arguments
      already published on its `TypeInfo`, so `*this` is that specialization
      without replacing the pattern bindings used for substitution. A nested
      class of an instantiation, and a dependent class-template pattern, still
      pop one flat pointer level. An array object without a published id fails
      closed. This does not close a 3A exit criterion.
      `tests/test_unary_pointer_array_identity_ret0.cpp` protects `*` of
      `int[2][3]`, `int[][2]`, a pointer to an array, `int**`, a const pointee,
      a const pointer object, a const array, `long` and `Cell` rows, a `short`
      matrix, and a class-template array member, plus `&` of those arrays,
      overloaded `operator*`/`operator&`, and a function address. The old `*`
      of an array object loaded the array bytes as a pointer.
      `tests/test_const_member_overload_on_const_param_ret0.cpp` exercises
      `*this` inside a partial specialization, which is now the canonical
      specialization rather than a popped pointer level. The class import uses
      its existing iterative worklist. The unary helpers are not recursive. They
      run after child expression inference returns and call the existing
      iterative materializer once. Codegen of `*` on an array
      object takes that element's address with one `AddressOf` or pointer copy,
      then loads only when the element is not itself an array. Native stack use
      does not grow with array rank or pointer depth. No stack reserve was changed.
   6. **Builtin `->`.** `resolveMemberAccessObjectInfo` no longer pops
      `pointer_levels` for arrow. `[expr.ref]` defines `E1->E2` as `(*(E1)).E2`,
      so the object type is the canonical builtin `*` pointee and is re-exported
      through `materializeTypeSpecifier`. One peel that still leaves a pointer
      or an array is not a class, so member lookup does not run. The dot path
      is unchanged. A nested class of an instantiation, and a dependent
      class-template pattern, still pop one flat pointer level. An array object
      without a published id fails closed. User-defined `operator->` stays on
      its existing lowering path. This does not close a 3A exit criterion; the
      3A rollup remains 10/79.
      `tests/test_arrow_pointer_class_identity_ret0.cpp` protects `Cell*` and
      `const Cell*` against `Cell* const`, arrow on `Cell[2]` reaching the first
      element, a pointer to a `Cell[3]` row, `long` and `short`, a class-template
      specialization, `this->` in a partial specialization, and an overloaded
      `operator->`. The helper is not recursive. Arrow runs after the object
      expression's type is inferred and uses the existing iterative materializer
      and class-import worklist. Native stack use does not grow with array rank
      or pointer depth. No stack reserve was changed. The sharded build is
      `/Od`, so there is no `-fstack-usage` number.
   7. **Builtin `new` and `new[]`.** The result is no longer a flat pointer
      pushed onto the allocated type. `[expr.new]` makes it
      `CanonicalTypeTable::pointer` of that type, or of the initial element when
      the allocated object is an array, re-exported through
      `materializeTypeSpecifier`. An array typedef loses one outermost bound.
      The runtime bound in `new T[n]` is not part of the type node, so `new
      Row[2]` stays a pointer to `Row`. A projectable pointer or array without
      a published id fails closed. An unmigrated non-projectable type still
      pushes one flat pointer level. Nested classes of an instantiation are not
      published here. This does not close a 3A exit criterion; the 3A rollup
      remains 10/79.
      `tests/test_new_pointer_array_identity_ret0.cpp` protects `new int`,
      `new const int`, `new int[n]`, `new Row` versus `new Row[2]`, `new Grid`
      versus `new const Grid`, `new int* const`, `new int*[n]`, `new Cell[n]`,
      `new Holder<long>`, and placement new. The helper is not recursive. It
      runs after the allocated type is canonicalized and uses the existing
      iterative materializer and class-import worklist. Native stack use does
      not grow with array rank or pointer depth. No stack reserve was changed.
      The sharded build is `/Od`, so there is no `-fstack-usage` number.

3. **Complete importer and declarator coverage.** An ordered declarator around
   an alias that is an array, a reference, a member pointer, or a function
   keeps that wrapper as the inner type.
   `tests/test_ordered_alias_wrapper_declarator_ret0.cpp` checks `Row (*p)[2]`,
   `Field (*p)[2]`, `Ref (*p)()`, and an ordered array alias under another
   declarator. `tests/test_ordered_function_alias_declarator_ret0.cpp` checks
   `F (*p)`, `const F*`, and a pointer to an array of function pointers. Keep member `TypeId`s
   preserved through every AST copy and substitution route. Remaining: a fully
   non-projectable ordered-spine `decltype` dereference (deferred to sema) and a
   call or xvalue operand whose value category the parser does not know at parse
   time. `UnsupportedStaticMemberType` (1020) remains a fail-closed guard for
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

These are unsupported or deferred today and seed adjacent slices; the
authoritative inventory is [known issues](KNOWN_ISSUES.md).

- Constraints: complex non-type concept argument expressions, constraints over
  packs or arrays, member/function dependent decorations, and forms that cannot be
  normalized leave candidates unordered; constrained free-function templates,
  packs, and non-type template parameters are unsupported in the
  address-deduction path; complex NTTP expressions, non-literal dependent
  arguments, non-type packs, and a C-style ellipsis combined with a pack are
  unsupported.
- Evaluated function-template address arguments still need IR materialization of
  the selected specialization.
- Virtual member-function addresses and multiple-inheritance member-pointer
  adjustment are unsupported, and calling through a materialized member-function
  pointer is unimplemented.
- The projectable pointer/array identity path now covers an unknown outer bound
  for builtin, dependent, specialization, record, and enum elements, and the
  dereference and multi-dimensional pointer-to-array subscript codegen consumers
  read it through one resolver. Conditional array decay and built-in subscript
  element extraction are structural-only, an array of class-template
  specializations now publishes identity, the identifier- and member-rooted
  multi-dimensional collectors read the resolver, and the member-access
  array-dimension helpers read `CanonicalTypeTable::arrayDimensions`. Codegen no
  longer reads any flat `pointee_array_declarator`/
  `has_unsized_outer_array_dimension` field; the remaining readers are
  semantic-internal descriptor construction and equality. Conditional array
  decay and published unary `*` and `&` export the canonical result instead of
  editing the operand projection. An implicit class-template specialization's
  `*this` uses that specialization. A nested class of an instantiation still
  pops one flat pointer level, and an array object without a published id
  fails closed. Builtin `->` re-exports that same canonical pointee; the dot
  path is unchanged. Builtin `new` and `new[]` re-export
  `CanonicalTypeTable::pointer` of the allocated type, or of its initial
  element when that object is an array.
- The compatibility `TypeTraitEval::isSigned`/`isUnsigned` adapters are
  unexercised; a regression forcing an unimportable operand through them is owed.
- The canonical record constructor schema is published for a completed record
  independently of the field schema. A constructor whose parameter types do not
  import structurally is skipped, and the schema stays unpublished (consumers
  fall back to `StructTypeInfo`) only when no constructor is importable. The
  field schema is still unpublished for anonymous-union and unimportable-member
  records and is not published for class-template specializations, which do
  publish the constructor schema. These are canonical-importer gaps, not language
  rules, and seed the constructor/member-function schema migration.
- An explicit (full) class-template specialization now shares the primary's
  canonical identity. Its declaration records the primary's `TemplateDeclId` in a
  dedicated `canonical_specialization_owner` field (kept out of `template_decl_id`
  so member lookup and classification are unaffected) and its concrete arguments
  in `outer_template_args`; `importCanonicalClassSource` then builds the same
  `TemplateSpecialization` an implicit instantiation would, and the record and
  constructor schema publish under that TypeId. This stays on the canonical path
  (no `UnmigratedNominal` fallback and no legacy `DeclarationBuilder` publish,
  pinned by `tests/test_canonical_constructible_explicit_specialization_ret0.cpp`).

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
  `materializeTypeSpecifier` round-trips the member pointer and the importer
  returns `Supported`. Remaining: dependent-address NTTP cases still return
  `Unresolved`, and the identity stamp should move into the remaining signature
  writers so the centralized step can be dropped. Trace:
  `checkMemberFunctionAddressAccessForTarget` (`SemanticAnalysis.cpp`) ->
  `tryImportCanonicalTypeDesc` -> `materializeTypeSpecifier` ->
  `set_function_signature`.
- **Negative tests:** encode the exact expected diagnostic ID multiset in the
  filename (`_e1001.cpp`, `_e1003_e1051.cpp`); `_fail.cpp` is reserved for the
  immutable legacy inventory.

## Active findings

- The top-level expression fallback can mask declaration-parse errors; see
  [known issues](KNOWN_ISSUES.md).
- Bit-field declarations still accept non-integral types, widths larger than
  the declared type, and named zero-width fields; static constexpr data-member
  NTTP arguments also fail outer specialization matching. Both are recorded in
  [known issues](KNOWN_ISSUES.md).

## Validation handoff

After compiler-source changes, build with `.\build_flashcpp.bat`; run focused
regressions while iterating, then `pwsh tests/run_all_tests.ps1` when ready.
Never run the full suite concurrently with the build.
After canonical identity changes, run
`python tests/architecture/run_canonical_types.py --mutations`; update changed
anchors while preserving each mutation's required rejection.

Run the host-native migration-counter and identity-inventory scripts under
`tests/migration_counters/` after compiler changes and keep every fixed-corpus
entry within baseline. `canonical_structural_trait_fallback` is 0 on the
structural-trait and lazy-constraint regressions (baseline lowered from 23 so a
reappearance fails); the residual lazy-constraint fallbacks are the
constructibility probes, which need the canonical constructor-query path. The
inline dollar-recovery inventory and the canonical-adapter source corpus remain
within their supported/deferred baselines. Gate 0's Windows and ELF
multi-translation-unit checks remain required compatibility evidence. See the
plan for complete boundary-specific validation.

For recursive-path changes, report the largest changed native stack frame and
whether stack use stays bounded as logical depth grows; do not raise the stack
limit to make a regression pass.

The concrete-argument rebinding guard compares clang-cl debug frames with
`5f86b217`: the largest changed recursive-path frame is
`materializeStoredTemplateArgs`, 17,128 versus 17,112 bytes.
`materializeDependentRecordTemplateArgs` is 6,632 versus 6,616; recursive alias
argument materialization remains 3,944 bytes and alias materialization 12,616.
The guards add no recursive traversal. Existing native recursion still grows
with logical depth; the 128-level alias and existing argument-depth limits
remain. Retain the 1 MiB native callable and canonical worklist guards.

Mixed callable-alias validation compares clang-cl debug frames with `5f86b217`.
The largest changed recursive-path frame is the shared signature helper at 488
versus 424 bytes; `parse_type_specifier` remains 226,104 bytes. The mixed-argument
substitution wrapper uses 152 bytes and the existing canonical worklist frames
are unchanged. Native guards check argument-kind positions, stale-projection
mutation, rejected kind mismatches, pack/unpublished-ID deferral, and 4,096 nested
parameter wrappers on a 1 MiB stack. Temporary substitution contexts remain in
scoped `FrontendContext` scratch storage; retain the reclamation and result
lifetime guards. No fields or semantic record types were added. Canonical
traversal is iterative; surrounding parser/substitution paths still recurse and
alias materialization retains its 128-level limit. These checks do not establish
bounded native stack use for the whole template pipeline. Source regressions
cover defaulted and interleaved type/value arguments, nested aliases, records,
noexcept callbacks, and executed conversion-result calls. The fixed adapter
corpus remains 26 supported and zero deferred; migration counters and the dollar
inventory remain within baseline.

Constructibility has one shared authority: `evaluateConstructibility` in
`TypeTraitEvaluator.cpp` answers the zero-argument and argument-bearing questions
and takes a `ConstructibilityFallback` (the lazy path asks `None` and keeps its
unknown result; the folded and codegen paths ask `Sema`); the canonical helpers
are file-local, and default-construction facts and unary structural properties
share one `CanonicalRecordFacts` mask
(`tests/test_constructibility_paths_equivalence_ret0.cpp` pins path agreement).
Canonical imports use `tryImportSupportedCanonical(table, syntax)`, returning a
`TypeId` only on `Supported` and an empty result otherwise (including `Invalid`,
which callers recover from); it takes the table explicitly because resolving it
through `requireFrontendContext()` pulls `FrontendContext.h` into a
widely-included header and breaks the canonical-type mutation harness.

The recorded Clang stack-usage probe measured `parse_declarator` at 5,160 bytes
versus 5,000 bytes on `origin/main`; repeat the comparison when changing
recursive parser paths. The latest canonical architecture probe reports
`TypeSpecifierNode` at 600 bytes.

The pointer/array materialization continuation compared standalone clang-cl
debug stack usage against `ffe47823`: `materializeTypeSpecifier` remains 1,704
bytes in the baseline and uses 1,720 bytes after migration. The shared in-place
base helper uses 248 bytes, while static-member materialization drops from 1,064
to 872 bytes. `exportCanonicalDeclaratorUntil` remains 568 bytes. Canonical
wrapper and base-qualifier traversal are iterative, so this migration adds no
native frame per pointer/array wrapper. It does not establish bounded recursion
in surrounding semantic paths. The adjacent member-CV writer uses 200 versus
184 bytes. A native regression round-trips 4,096 pointer wrappers through the
semantic query boundary without invoking the recursive parser. Both new native
materialization fixtures pass in a copied test executable reduced to a 1 MB
stack; no compiler or project stack reserve was increased.

The parser-side receiver/member CV propagation changes the
`apply_member_access_value_category` lambda frame from 72 to 104 bytes; the
enclosing `Parser::get_expression_type` frame remains 44,856 bytes. That function
still recursively visits nested expressions and receivers, so native stack use
grows with expression nesting. This migration adds no new recursive walk and
does not establish bounded parser stack usage.

The nested-callable continuation compared standalone clang-cl debug frames with
`9d2491b8`: the call-result inference visitor remains 1,896 bytes and its enclosing
`inferExpressionType` remains 760 bytes. The new receiver-chain worklist helper
is 888 bytes; its native frame does not grow with the number of callable results.
The largest changed lowering helper is `buildFunctionSignatureReturnType` at
1,224 versus 1,288 bytes; return-index reconstruction is 936 versus 376 bytes.
The unchanged enclosing member-call lowering frame is 44,664 bytes. A 16-level
callable-chain regression exercises the retained signatures. Existing expression
normalization and lowering still recurse, so these measurements do not establish
bounded native stack usage for the entire compiler or logical template depth.
The chain passes with the shipping stack reserve unchanged; a separate 1 MB
reduced-stack executable overflows in the existing primary-expression parser
before reaching sema (recorded in `KNOWN_ISSUES.md`).

The constructor/reference continuation compared Clang 20 debug frames against
`e590ccc7`: constructor lowering is 27,384 versus 27,192 bytes, function-call
lowering 20,488 versus 20,216, reference materialization 3,976 versus 3,608, and
semantic reference binding 3,960 versus 3,704. The shared structural reference
planner is 600 versus 328 bytes. The new traversals are iterative; a 4,096-level
qualification regression passes with a 1 MB stack. Existing nested expression
lowering still uses native recursion, and these checks do not prove all legacy
template paths bounded. The full native suite has the same two failures on the
unchanged baseline; see [known issues](KNOWN_ISSUES.md).

The member-type continuation publishes the complete declared canonical TypeId
at member construction, including template specializations, anonymous records,
union members, lambda captures, and synthetic local-array initialization
metadata. Registration and layout rebuilds take the ID as a required argument;
there is no follow-up write through `members.back()` to finish type identity.
Resolved array bounds are imported together with the original declaration's
pointee and intermediate pointer qualifiers. The shared compatibility
materializer serves parser queries, semantic analysis, constexpr initialization,
and member-array address sizing. Redundant constexpr projection wrappers were
removed. `StructMember` remains 320 bytes on the measured Windows x64 build.

Canonical reference binding now preserves source object qualification when
expression value category is carried outside the canonical declarator spine.
This selects `int* const&` for const pointer members and preserves the direct
binding's address producer, fixing the missing temporary-storage failure without
adding a lowering fallback. Member-array address generation also consumes the
full element type, preserving pointer-width strides and the flattened index of
multidimensional arrays. Regressions cover pointee/intermediate CV, arrays,
references, inheritance, self/forward declarations, primary and partial/full
specializations, typedef records, anonymous unions, captures, callable members,
and mutation through references to nonzero pointer-array elements. An exact
diagnostic regression rejects binding a const pointer member to `int*&`.

This is still a staged migration. `StructMember::type_index` and the equivalent
anonymous-union field remain compatibility handles for base category, legacy
TypeInfo lookup, and layout consumers. Unsupported/dependent canonical imports
can explicitly retain an empty member ID; canonical record-subobject publication
then stays unavailable rather than reconstructing an approximate declared type.
Reverse materialization for specialization/dependent bases and the remaining
flat consumers must migrate before these handles and redundant shape fields
can be removed. The [follow-up agent prompt](member-type-migration-followup.md)
describes that work. The separate narrow nullptr-reference store and inline
typedef-record function-pointer parser failures remain in `KNOWN_ISSUES.md`.

Clang debug stack-usage comparison against `79df3ff8` reports the largest
changed recursive frame in `try_instantiate_class_template`: 409,464 bytes
versus 409,352. The struct parser is 125,608 versus 125,496 bytes, lambda parser
24,776 versus 23,512, address-expression analysis 2,296 versus 1,432, and array
subscript lowering 22,952 versus 21,672. The new member materializer is 376
bytes; member import overloads use 664/936 bytes. Wrapper traversal stays
iterative, but existing source/template recursion still grows native stack
usage with logical nesting. These measurements do not establish bounded stack
usage for the whole compiler. No stack reserve was changed.

### Linux member-type CI continuation

The Linux pointer-reference overload failure also exposed an Itanium mangling
collision: the legacy encoder emitted pointer constructors before references
and discarded referent pointer CV as if it were a by-value parameter qualifier.
It now emits outer constructors first, traverses legacy pointer levels from
outermost to innermost, and retains CV through references. Ordered and legacy
encoders share the ABI qualifier ordering. Native exact-encoding checks include
4,096 pointer levels in both representations, under the normal test stack.

The callable-array regression's aggregate result exposed a separate missing
arithmetic conversion. `Holder<long long>{int_value}` tagged the source as a
64-bit member store while leaving its producer 32-bit, reading uninitialized
upper bytes. Shared nested, array-member, default-member, and synthesized
default-constructor stores now use the existing numeric conversion helper.
The regression covers signed and unsigned widening, template records, mixed
member widths, scalar braces, arrays, float-to-double, and default initializers.
The local aggregate store boundary still needs migration, as documented in
`KNOWN_ISSUES.md`; nested aggregate and contextual-bool gaps found during the
continuation are recorded there too.

Clang 18 Linux stack probes against `bdf4411f` measure the largest changed
recursive frame, `generateNestedMemberStores`, at 7,368 bytes versus 7,192
(+176). Its existing native recursion still grows with aggregate nesting;
attempted deep runtime aggregate coverage encountered the documented preexisting
initialization gaps. The nonrecursive default-constructor emitter is 10,008
versus 9,864 bytes; the new conversion helper is 72 bytes. Legacy Itanium
encoding grows by 16 bytes to 2,040 bytes for `std::string`, while the ordered
encoder stays at 3,272. Pointer-wrapper traversal remains iterative, and no
stack limit or reserve was changed.
