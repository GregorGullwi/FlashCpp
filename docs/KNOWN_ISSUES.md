# Known Issues

## Invalid aggregate base initializers can reach IR lowering

An empty aggregate base requires an explicit brace clause before later member
initializers, for example `struct Empty {}; struct Derived : Empty { int x; };`
with `Derived value{{}, 1};`. The invalid `Derived value{1};` can reach the
shared aggregate emitter without a semantic diagnostic and fail internally
while resolving a base constructor. Validate aggregate clause appertainment
and base initialization before lowering, with a source-located diagnostic.

## Implicit derived copy/move construction can omit a template base

An instantiated `template<class T> struct Base { T value; };` can have no
constructor AST even though its implicit copy/move operations exist. Copying
or moving a derived object can then leave `Base<short>::value` uninitialized
while copying its other bases and direct members. The implicit derived
constructor emitter currently skips bases for which `hasAnyConstructor()` is
false. Materialize or plan the implicit base copy/move operation before
lowering, preserving nontrivial member construction and base offsets rather
than replacing the operation with an unconditional byte copy.

## Conversion-function value base tails need base-object materialization

A conversion function returning `Derived` can rank a trailing conversion to a
by-value `Base` parameter, but lowering does not materialize the selected base
copy/move construction. For example, with `struct Base { long long value; };`,
`struct Derived : Base { short tag; };` and an operator returning a `Derived`
whose `value` is 42, passing that conversion result to `long long read(Base b)`
can lose the value. Reference targets preserve it. The conversion-operator call
helper currently routes the tail through `generateTypeConversion`, which lacks
class-value slicing. Boundary 9 must carry the selected base constructor into
lowering and reuse the base-object materialization path; a raw byte copy would
incorrectly bypass nontrivial copy/move semantics.

## Remaining structural NTTP shapes need canonical value import

Union declaration groups now carry explicit active-member state. Aggregate,
designated, default, and constructor initialization plus member assignment mark
the selected variant; `StructuralClassValue::union_selections` keeps the group
start and selected declaration ordinal (or explicit no-active); equality, hash,
rehydration, and the canonical importer include it. The reduced
`Key{.first=0}` / `Key{.second=0}` case now produces distinct template arguments,
protected by `test_canonical_structural_nttp_union_active_ret0.cpp`.

Nested anonymous-union-in-anonymous-union hierarchy still needs an explicit
representation; flat contiguous layout groups alone cannot distinguish an
inactive outer union from an active outer variant containing an inactive inner
union.

Float and double fields, including array elements, now use exact typed
canonical floating values; equivalent syntax and declaration imports share
identity. Long double remains unsupported for structural values because the
evaluator carries only a 64-bit representation, so it fails closed instead of
aliasing through double bits. `std::nullptr_t` fields use a canonical
`NullptrValue` keyed by the nullptr builtin.

Null object, function, and member pointer fields now use typed canonical null
values, and null pointer-like array elements carry the declared element pointer
depth through structural rehydration. Direct and unambiguous non-virtual
inherited member-object pointer fields resolve to a declaring-class TypeId plus
member ordinal, and member-object-pointer arrays preserve their outer array
wrapper and element targets. Direct and inherited member-function pointer fields,
including virtual functions, and member-function-pointer arrays use a
declaring-class TypeId plus declaration ordinal, with overloads selected by
canonical signature. Non-null object/function pointer targets, ambiguous or
virtual-base member pointers, and non-null object/function pointer array
elements still need declaration identity.

Plain implicit class-template specializations now import as structural values
through their canonical specialization TypeId. Specialization-owner member
pointers recover member names/ordinals from the published primary pattern record,
verified by declaration pointer; the specialization TypeId remains the value
identity. Specializations whose owner schema is needed for base or union facts
still fail closed when that schema is not published by specialization identity.
Extend the typed-value importer before deleting `legacyDerivedBaseOffset`;
audit dependent and other non-integral specialization roots as well.
Integral, bool, and enum fields, including named bit-fields with signed
extension or unsigned truncation, share class/type/value identity between
syntax and declaration imports for equivalent named and braced values. Enum
values use their published underlying layout. Unnamed and zero-width bit-fields
remain declaration/layout padding and never add value subobjects.
Array fields now use ordered canonical array values with the published element
type and outer bound; nested arrays and record elements recurse through the same
value worklist. Elements are converted to the declared element type before
identity, so equivalent member spellings share one value.
Retain `test_canonical_structural_nttp_base_source_ret0.cpp`,
`test_canonical_structural_nttp_equivalence_ret0.cpp`,
`test_canonical_structural_nttp_subobjects_ret0.cpp`,
`test_canonical_structural_nttp_enum_fields_ret0.cpp`,
`test_canonical_structural_nttp_bitfields_ret0.cpp`,
`test_canonical_structural_nttp_arrays_ret0.cpp`,
`test_canonical_structural_nttp_union_active_ret0.cpp`,
`test_canonical_structural_nttp_float_ret0.cpp`,
`test_canonical_structural_nttp_null_pointer_ret0.cpp`,
`test_canonical_structural_nttp_null_pointer_array_ret0.cpp`,
`test_canonical_structural_nttp_class_template_ret0.cpp`,
`test_canonical_structural_nttp_member_object_pointer_ret0.cpp`,
`test_canonical_structural_nttp_member_function_pointer_ret0.cpp`,
`test_canonical_structural_nttp_inherited_member_object_pointer_ret0.cpp`, and
`test_canonical_structural_nttp_overloaded_member_function_pointer_ret0.cpp`,
`test_canonical_structural_nttp_member_pointer_array_ret0.cpp`,
`test_canonical_structural_nttp_member_function_pointer_array_ret0.cpp`, and
`test_canonical_structural_nttp_inherited_member_function_pointer_ret0.cpp`,
`test_canonical_structural_nttp_virtual_member_function_pointer_ret0.cpp`, and
`test_canonical_structural_nttp_specialization_member_pointer_ret0.cpp`, and
`test_canonical_structural_nttp_nullptr_member_ret0.cpp` as current guards.

## Scalar aggregate base initializers still fail IR lowering

Constexpr evaluation now materializes a scalar base-class initializer such as
`Key value{7, {1}}`, but IR conversion still reports “No matching constructor
for aggregate base expression initializer” for the same declaration. Braced base
initializers (`Key value{{7}, {1}}`) lower correctly. Fix the aggregate base
initialization lowering path rather than reintroducing a constexpr-only
workaround.

## Explicit casts of overloaded member-function addresses lose target identity

`static_cast<int (Object::*)(int)>(&Object::first)` evaluates without retaining
the selected overload target, so different overloads can collapse to one
structural value. Target-typed initialization such as `IntKey key{&Object::first}`
resolves correctly. Canonical structural import now selects overloads by
signature when the target name survives; fixing the cast path belongs to
expression sema (boundary 4).

## Static constexpr data-member NTTP arguments fail specialization matching

A static constexpr data member used directly as a template argument can be
imported, but using it as an argument to another class template does not
participate correctly in specialization matching. For example:

```cpp
struct Key { int value; };
struct Holder { static constexpr Key key{5}; };
template<Key Value> struct Base {};
template<class A, class B> struct Same { static constexpr bool value = false; };
template<class A> struct Same<A, A> { static constexpr bool value = true; };
static_assert(Same<Base<Holder::key>, Base<Key{5}>>::value);
```

The direct `Base<Holder::key>` instantiation succeeds, but the `Same`
comparison fails to resolve the selected specialization and reports an
undefined qualified identifier. This affects scalar and bit-field members
equally and is independent of the bit-field value conversion. Materialize the
static member's typed value identity before building the outer specialization
key.

## Ordinary calls can prefer a generic NTTP function template

For `template<unsigned N> struct Cell {};`, ordinary call selection can prefer
`choose(Cell<N>*, T*)` over `choose(Cell<1u>*, T*)` when passed a `Cell<1u>*` and
another pointer. Both are function templates; the fixed-value overload should
win. This path still uses `computeFunctionTemplateSpecificity`, which does not
order the constant and dependent NTTP patterns. The canonical function-template
address-query path supports typed integral ordering, but ordinary calls need
that deduction-based comparison in place of the specificity score.

## Expression parsing can overflow a 1 MB native stack before callable sema

The 16-level `test_deep_callable_chain_ret0.cpp` passes with the shipping Windows
compiler's existing stack reserve. A copy of that executable with its reserve
reduced to 1 MB overflows in `parse_primary_expression`, reached through
`parse_function_arguments`, before semantic analysis. The callable-result type
inference worklist adds no native recursion for the receiver chain, but does not
resolve these existing parser frames or recursive expression normalization and
lowering. No stack reserve was increased for this regression.
The native `Canonical adapter imports the source corpus at publication` fixture
also overflows at 1 MB, including in a pre-change binary. Reduce parser frames
and replace recursive syntax processing before expecting that corpus to fit.

## SEH finally funclet termination is conservative in return-flow diagnostics

The control-flow analysis models a Windows `SehFinallyCall` as a call that can
return normally or throw to the active enclosing handler. It does not yet
summarize its out-of-line `__finally` body. Consequently, a value-returning
function whose finally body always throws, calls a `[[noreturn]]` function, or
loops forever can receive a conservative missing-return warning. SEH is a
target extension, separate from the C++20 try/catch paths that are analyzed
directly. A future implementation should compute funclet return/throw summaries
and use them at each invocation, preserving its individual continuation and
exception context.

## Deep aggregate initialization still uses native recursion

The shared aggregate member-store emitter recursively descends into base
subobjects, nested members, default member initializers, and struct array
elements. Its array shape and initializer-list walks also use native recursion.
Consequently, native stack usage grows with source aggregate nesting; the
current parser also has recursive expression frames. Ordinary nested structs
and array members work, but sufficiently deep inputs can exhaust the stack.

A future refactor should use continuation frames on an explicit worklist for
aggregate and array traversal together. Frames must resume each parent after
its child finishes, preserving base, member, and array element initialization
order, including constructor and default member initializer side effects.

## WSL front end crashes while processing the libstdc++ `<typeinfo>` test

Reproduced on 2026-10-06 on Ubuntu 24.04 under WSL2 with a fresh Debug compiler
build (`make main CXX=clang++`). Running
`x64/Debug/FlashCpp tests/std/test_std_typeinfo_ret0.cpp` preprocesses the
header, then SIGSEGVs (exit 139) before producing an object. GDB places the
fault in `std::optional<ASTNode>::has_value()` on a null optional, called by
`ConstExpr::Evaluator::resolve_constexpr_member_source_from_initializer` for
`__name` while evaluating the array subscript in `type_info::name()`. The
evaluation occurs during IR generation through
`AstToIr::tryEvaluateAsConstExpr<TernaryOperatorNode>`. This front-end failure
means the test still does not reach its Itanium symbol-mangling check on WSL.

## Complex member-type class-template bases still need stack coverage

The base-instantiation worklist now expands concrete pack-expanded bases and
schedules member-type chains that pass through plain nested classes and end in
a non-template alias to a class-template specialization. The 1,024-level
regressions `tests/test_deep_pack_expanded_base_chain_ret0.cpp`,
`tests/test_deep_member_type_base_chain_ret0.cpp`, and
`tests/test_deep_nested_member_type_base_chain_ret0.cpp` cover those paths;
the pack and alias regressions also pass at their existing shallow depths.
These checks use the shipping Windows compiler build settings, with no
stack-reserve change.

Member-template segments and member aliases whose targets are not directly
materialized class-template specializations still fall back to ordinary
instantiation. Stack usage for deep chains through those forms remains
unverified. Extend dependency scheduling to cover them while sharing base
substitution semantics with ordinary instantiation.

## Legacy flat consumers cannot yet handle interleaved pointer/array declarators

Boundary 3A is not complete: descriptors carry authoritative
`structural_type_id` identity where canonical import is supported and retain
flat fields as compatibility projections, but unmigrated consumers still read
those fields directly. A non-projectable pointer/array type-id is kept on the
stored template argument as its ordered declarator, and a declarator written
around a template parameter is kept on substitution. General IR object-storage
consumers still read parallel flat pointer/array fields, so member storage of
an interleaved declarator remains fail-closed. Built-in subscripting no longer
flattens an interleaved chain: each selected step is materialized from the
sema-published canonical result type, so an array of pointers followed by a
pointer-to-array uses the correct pointer/row strides. Inline array-object
storage, identifier lowering, and initializer-list lowering now accept
non-projectable interleaved array declarations with a known outermost bound.
Each brace clause initializes one outermost element, and a short list
value-initializes the rest. An unknown outermost bound is completed from a
brace list of n > 0 clauses as an array of n elements. A range-for over that
array visits one outermost element through a pointer to that element. The remaining flat
readers still fail closed.
An ordered declarator around an alias that is an array, a reference, a
member pointer, or a function keeps that wrapper as the inner type. An array
of a function alias, or a function returning one, is ill-formed. An ordered member-function-pointer declarator whose
callable return carries its own declarator (for example
`int* (Owner::*)() const`) is now supported end to end: it imports structurally
with the owner `TypeId`, ABI-mangles in both the MSVC and Itanium styles, and
lowers its fixed-size scalar object (declaration, parameter, return, and
identifier paths). Multi-hop pointer-to-array alias
forwarding keeps the pointee extent; an eight-hop chain is pinned by
`tests/test_alias_template_forwarded_pointer_to_array_bounds_deep_ret0.cpp`.
Non-projectable spines are rejected at migrated boundary guards rather than
being reordered or truncated. Remove this entry when those consumers migrate
and the compatibility projection fields are deleted.

The shared unary type-trait evaluator now classifies the whole structural
`[meta.unary.prop]` family from the canonical `TypeId`, and the lazy-constraint
evaluator routes the same family through that classification, so
`tests/test_canonical_structural_type_traits_ret0.cpp` and
`tests/test_canonical_lazy_constraint_traits_ret0.cpp` no longer depend on the
flat fields for pointers, arrays, references, functions, member pointers, enums,
or the builtin groupings, and
`tests/test_canonical_class_qualification_traits_ret0.cpp` adds the class, union,
cv, and signedness families. Trait operands now publish their nominal `EntityId`
at materialization, so `canonical_structural_trait_fallback` is zero on the
structural-trait regressions and its recorded baseline was lowered from 23 so a
reappearance fails the counter run. The triviality and lifetime families still
read `StructTypeInfo`, which needs a published member-property schema rather
than a classifier change. Two measurable trait residues remain: the
`lazy_constraint_trait_fallback` counter, which records the traits the canonical
table does not own, and the residual 2 on
`tests/test_canonical_lazy_constraint_traits_ret0.cpp`, which is a
member-object-pointer operand: that one needs member-owner identity rather than
nominal type identity, and its owner is not published at materialization.
The canonical record-properties schema now publishes polymorphic, final, and
abstract facts for completed record TypeIds, including materialized
class-template specializations. The
`test_canonical_record_class_trait_concepts_ret0.cpp` regression checks their
C++20 answers in constant evaluation, runtime lowering, and lazy constraints,
with both canonical-trait fallback counters at zero. Class-template
instantiation now refreshes virtual metadata and object layout after member
declarations are attached, then publishes the specialization's canonical facts.
The remaining sema-owned construction traits (`__is_constructible`,
`__is_trivially_constructible`, and `__is_nothrow_constructible`) depend on a
variadic argument list and overload resolution, so they need a canonical
constructor-query path rather than a unary record-property flag. The unary
record-property traits now read TypeId-keyed facts published for completed
records and materialized class-template specializations. The
`test_canonical_record_property_trait_concepts_ret0.cpp` regression covers
their constant-evaluation answers, positive and negative class shapes, template
specializations, and lazy concept constraints. By contrast,
`__is_class`, `__is_union`, `__is_const`, `__is_volatile`, `__is_signed`, and
`__is_unsigned` now use canonical type identity. A concept built on an
unsupported sema-owned trait still receives an explicit unknown outcome: not
proof of satisfaction, and not inverted into a failure by `!`, `&&`, or `||`.

Ordered declarators over aliases with callable, array, reference, or
member-object-pointer wrappers now convert as the spelled type: copy
initialization, parameter passing, and overload ranking consume the ordered
spine. Static member semantic identity must remain canonical and must not be
flattened.

## Production speculative parsing is not yet integrated with frontend scratch transactions

`FrontendScratchTransaction` now journals frontend scratch state,
`DeclarationBuilder`, `TemplateDeclTable`, namespace registry creation and
metadata, publication maps on `SymbolTable`s bound to its `FrontendContext`,
and the in-place array-type normalization used when compatible global or
namespace variable declarations are merged.
Nested commits remain provisional until the outer transaction commits. The
transaction does not roll back scope creation or cursor movement in `SymbolTable`
or `ScopeRecord`, and production parsing does not yet route tentative declaration
work through it. Integrate those boundaries before relying on the transaction
for broad speculative parsing. `gNamespaceRegistry` is still process-global,
while symbol journals are enlisted per `FrontendContext`: transactions spanning
multiple live contexts are not isolated, and rolling back one context can undo
namespace changes whose symbol-table writes belong to another. Keep publication
transactions within one context until registry ownership or journal enlistment is
moved to a shared coordinator.

## Member-function-pointer address-of only lowers non-virtual, non-adjusted cases

`int (S::*p)() = &S::f;` now materializes a runtime member-function pointer for
the common case: sema records the selected overload on the address expression
and IR lowers it to the member's code symbol. The representation is the
compiler's existing 64-bit member pointer, so this covers non-virtual members
whose address needs no `this` adjustment (single-inheritance, no virtual
dispatch). Structural NTTP identity for virtual member functions is now
canonical, but runtime lowering and calls through virtual member pointers remain
unsupported here. Calling through such a pointer with `obj.*p` or `ptr->*p` lowers as
an indirect call that passes the object as `this` followed by the explicit
arguments. The call reads the member pointer's canonical owner `TypeId`; when
that owner is a base of the object, it applies the published non-virtual or
virtual-base adjustment before the call.
`tests/test_member_function_pointer_call_ret0.cpp` covers mutable and const
members, both access forms, and reassigning the pointer, while
`tests/test_member_function_pointer_call_base_adjustment_ret0.cpp` covers a
non-primary base and a virtual base. Global member-function-pointer
initializers consume the same sema-resolved address, including an instantiated
member function template, covered by
`tests/test_member_function_pointer_global_address_ret0.cpp`.

The following remain unsupported and are rejected or left unresolved:

- Taking the address of a **virtual** member function throws
  `CompileError`; the 64-bit representation cannot carry the vtable-index
  encoding the ABI requires.
- Address-of does not encode a `this` adjustment for a member pointer whose
  target owner is a non-primary base. Calls adjust only when the pointer's
  canonical owner is already a base of the object.
- Some member-template address overload shapes can still route instantiation
  through a parser-owned root. `ensureMemberFunctionAddressMaterialized` logs a
  warning and leaves the address unresolved in that case rather than failing
  the translation unit, so the old uninitialized-slot behavior can still appear
  there.

## Static-member template initializer replay still re-parses source text

Variable-template initializers now substitute structurally from the
declaration-time AST, but four sibling lexer-replay sites still substitute
static-member initializers by re-parsing source text (in-class primary and
partial-spec, and out-of-line clones in
`Parser_Templates_Inst_ClassTemplate.cpp`, plus the lazy clone in
`Parser_Templates_Lazy.cpp`). Migrating them onto structural substitution
requires the same capture guarantee for class-scope alias bodies plus the
member-context replay metadata those paths rely on. `Parser::ReplayTemplateBindings`
(pack-aware) keeps pack arity correct inside those replays meanwhile.

A structural-first flip of the in-class primary clone was attempted and
reverted (2026-08-22); it regressed two tests and exposed the exact ambient-
state coupling that replay currently absorbs:

- `test_template_default_qualified_arg_order_ret42.cpp` returned 44 instead of
  42. Trace chain: holder's NTTP default `pair_value<Z, A>::value` is evaluated
  by `substituteNonTypeDefaultExpressionImpl` with the correct frame
  (`params=Z,A,V args=4,2`) and correctly rewrites to
  `pair_value$eed27b09::value`; immediately afterwards a second substitution
  pass resolves the same stored owner arguments with `A -> 4` instead of 2,
  instantiates a bogus `pair_value<4,4>` whose member early-normalizes to 44,
  and that value becomes V. The wrong binding appears only when replay does
  not run first: some later re-resolution of the struct's stored instantiation
  context (early-normalizer / constexpr member lookup path) reads bindings
  polluted by an enclosing frame. Root cause to fix: make the instantiation
  context stored on completed class specializations authoritative (concrete
  values, not dependent spellings) and stop later passes from re-deriving
  member initializers through ambient `template_param_substitutions_`.
- `test_template_dependent_base_member_template_static_value_ret0.cpp` failed
  to link (`Derived$hash::value` unresolved): the structural result for a
  static member reached through a deferred base did not register the emitted
  global under the name main references.

Migration precondition for both: the capture guarantee already built for
variable templates must hold for class-scope alias bodies, plus a
member-context equivalent of the definition-lookup metadata replay installs
(`push_replay_member_context`). A strict post-substitution dependency check
(param-name identifiers, TemplateParameterReferenceNode, dependent call
records) is the right flip signal once those are fixed.

## Template instantiation recursion has very high per-level stack cost

The recursive base-class instantiation path
(`try_instantiate_class_template` → `materializeTemplateInstantiationForLookup`
→ `instantiate_and_register_base_template`, plus the substitution machinery it
calls) consumes roughly 400KB of stack per inheritance level in `-O0` builds,
because the substitution functions keep many inline-storage locals on the stack.
A `Chain<39>`-style test needs ~17-20MB, which is why
`ensureMinimumProcessStackSize()` raises the Linux soft `RLIMIT_STACK` to 64MB
(src/FlashCppMain.cpp). The existing depth guard
(`kMaxTemplateInstantiationNestingDepth = 128`) only counts nesting levels and
cannot fire before the process stack is exhausted, so a chain roughly three to
four times deeper than `Chain<39>` would still overflow. Slimming the fat frames
(inline-storage locals in `substituteTemplateParametersWithState`,
`instantiate_and_register_base_template`) or converting fatal exhaustion into a
clean diagnostic would remove this ceiling.

## Current `<any>` / `<deque>` integration stops

The 2026-08-16 standard-header probes still fail outside this Phase 1 scope.
Both `tests/test_std_any.cpp` and `tests/test_std_deque.cpp` first report that
all template overloads for `_Seek_to` failed, followed by codegen diagnostics in
the MSVC `view_interface` implementation because `operator==` is unavailable.
These are generic constrained iterator/member lookup and operator-materialization
gaps; the compiler does not special-case those library names.

## Non-standard layout/constexpr acceptance gaps tracked as compatibility tests
These tests are intentionally kept in compatibility form so the current FlashCpp
suite stays green, even though they are not strictly standard-conforming under a
pedantic C++20 compiler.

- `tests/test_constexpr_offsetof_nested_ret0.cpp`
- `tests/test_constexpr_offsetof_ret0.cpp`
- `tests/test_identifier_binding_constexpr_function_call_member_access_prefers_static_member_function_ret42.cpp`
- `tests/test_infer_expr_type_expansion_ret0.cpp`
- `tests/test_no_unique_address_empty_member_same_type_overlap_ret0.cpp`
- `tests/test_outofline_nested_pack_ret0.cpp`
- `tests/test_outofline_nested_union_ret0.cpp`
- `tests/test_sizeof_offsetof.cpp`

## Constexpr evaluation does not yet expose standard semantic outcomes
The constant-expression pipeline still reports most unsuccessful evaluations as
one generic evaluator failure. It does not consistently distinguish these C++20
outcomes:

- the expression is still dependent and must be checked after substitution;
- substitution produced an ill-formed expression that requires a diagnostic;
- the expression is well-formed but is not a constant expression;
- the expression is a valid constant expression that the evaluator does not yet
  implement.

Template static-member normalization therefore still has phase-specific recovery.
`tryEarlyNormalizeTemplateStaticMemberInitializer(...)` returns no normalized
initializer for a generic evaluation failure, while `UnresolvedSizeofPolicy`
only makes unresolved/incomplete `sizeof(type-id)` a hard error during the final
retry for `constexpr` static members. C++20 validity is not determined by that
declaration flag or retry phase: a non-dependent invalid `sizeof` operand is
ill-formed whenever the specialization requires it.

The compatibility boundary is exercised by dependent NTTPs, recursive static
members, hidden-friend calls, and nested constexpr member/helper access, including:

- `tests/test_function_template_dependent_identifier_nttp_ret0.cpp`
- `tests/test_template_recursive_static_constexpr_member_ret0.cpp`
- `tests/test_template_static_constexpr_dependent_hidden_friend_ret0.cpp`
- `tests/test_template_static_member_initializer_helper_member_access_ret42.cpp`
- `tests/test_template_static_member_initializer_nested_constexpr_member_call_ret42.cpp`
- `tests/test_template_static_member_initializer_nested_helper_access_ret42.cpp`

These tests pass through the current staged replay/substitution/evaluation
pipeline, but a blanket conversion of every unsuccessful early evaluation into a
diagnostic regresses them. The long-term fix is a structured evaluation result,
standard point-of-instantiation checking for dependent expressions, and complete
constexpr call/member-access evaluation. Invalid semantic states should then
produce `CompileError`; missing canonical compiler metadata should produce
`InternalError`; unsupported evaluator coverage must not be accepted as either a
constant value or an ill-formed program.

## SysV x87 aggregate return gap

Concrete SysV aggregate returns plan `direct` vs `indirect` from canonical layout
classification during IR generation, including single-eightbyte SSE-only values
such as `struct { float; }` / `struct { double; }` that must use XMM0 rather than
the legacy integer return path. Aggregates larger than two eightbytes, and ≤16-byte
MEMORY-class values such as unaligned packed aggregates, select a hidden return
slot. Incomplete or placeholder return types are `dependent` and must not be
treated as direct; concrete function/call IR requires a resolved plan and surfaces
missing canonical metadata as `InternalError`.

Aggregate returns containing `long double` remain on the legacy path. Their SysV
result classification uses the X87/X87UP classes (`%st0` / paired X87UP), which the
current backend return-register abstraction cannot represent yet. Aggregate
parameters containing `long double` are still classified as MEMORY as required.

This gap is specific to `long double` (not `float`/`double` SSE aggregates). Closing
it is blocked on broader `long double` codegen support. LLP64 sizing and literals
now consistently use the Microsoft x64 64-bit `double` representation, including
bit-preserving builtin bit-casts, but direct `long double` floating comparisons
still have an LLP64 lowering discrepancy. LP64 has type identity and some
constexpr/overload/builtin coverage, but no real x87 load/store/`%st0` emission
path. Do not paper over return ABI with size guesses or INTEGER
fallbacks; wait until `long double` lowering can emit the SysV x87 convention.

`long double` is not a usable type today, and that constrains how it may be used
elsewhere. The collapsing is the general model rather than a constant-expression
corner case: `sizeof(long double) == sizeof(double)` on every target, locked in
by `tests/test_windows_long_double_abi_bit_cast_ret0.cpp`, which also runs and
passes in the Linux suite. Measured on 2026-09-30, ordinary runtime arithmetic is
wrong - `long double a = 3.0L, b = 4.0L; a + b == 7.0L` evaluates false. A
`long double` here is the `double` representation under a distinct name that the
backend cannot compute with.

Consequences for other work: a trait regression must not assert a `long double`
property as if it exercised the x87 type, because every such property is
insensitive to the collapsing and merely restates the `double` answer. Keep
floating-point coverage on `float` and `double` and say why `long double` is
excluded. A type-level test that genuinely distinguishes the two, such as
`__is_same(long double, double)`, would pin the defect rather than hide it, and
is owed.

## Recursive class-template chains can overflow the native stack

A generated benchmark probe using a recursively specialized class template
whose static constant references `DepthValue<N - 1>::value` overflowed the
shipping Windows compiler stack at shallow logical depth. The crash recursed
through class-template materialization and expression substitution rather than
producing an implementation-limit diagnostic. The throughput corpus avoids
this construct; the query benchmark retains a separate 1,025-level logical
dependency probe. Architecture boundary 7 must move the real instantiation and
substitution path onto small arena-owned frames before this issue can be closed.

## Several constrained function templates in one translation unit mis-resolve

Overload resolution between two constrained function templates becomes
order-dependent once enough constrained templates coexist in a translation
unit. With one constrained overload and an unconstrained `...` fallback per
probe, adding an eighth or ninth constrained probe function makes an earlier
probe select its fallback, and swapping the declaration order of two
mutually-exclusive constrained overloads changes the selected candidate. The
failure is independent of the canonical type work: it reproduces on
`origin/main`, and it is not tied to which trait the constraint names. Concept
partial ordering and constraint-based tie-breaking need their own boundary-5
slice; until then a constrained-overload regression must stay within the
prose-verified probe count and pair each constrained overload with an
unconstrained fallback rather than with a second constrained overload.
