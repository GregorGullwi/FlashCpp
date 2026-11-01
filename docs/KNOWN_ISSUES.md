# Known Issues

## Nested methods under selected member partial specializations can miss emission

A nested class inside a selected member partial can parse its outer-dependent
method but leave an unresolved method symbol at link time. Reduced shape:
`template<class T> struct Owner { template<class U> struct Box {};
template<class U> struct Box<U*> { struct Child { T echo(T value) { return value; } }; }; };`.
After namespace aliases for `Owner<long long>::Box<int*>` and its `Child`, calling
`child.echo(5000000000LL)` produces an unresolved `echo` on Windows. Direct member
partial methods have separate signature coverage. Audit nested-class binding
propagation and deferred body ownership before enabling this nested case; do not
recover the method by generated name. The selected-partial lazy nested setup
currently pairs effective parent parameters with local-only arguments.


## Anonymous unions in partial class-template specializations lose their declarations

The separate partial-specialization body parser in `Parser_Templates_Class.cpp`
does not retain anonymous-union members in the pattern AST. For example,
`template<class T> struct Partial<T*> { union { T first; T second; }; unsigned char tag; };`
after a primary declaration gives `Partial<long long*>` the wrong size and omits
its union fields. Reuse the normal class-member parser before publishing these
specializations' field schemas; layout publication cannot recover absent declarations.
Primary templates and their ordinary nested classes retain these static facts.

## Remaining dependent member-template field forms need concrete TypeInfo

Direct type packs, supported nontrivial type-pack patterns, and lockstep
expansions across enclosing and primary member-template packs now materialize
concrete field TypeInfo and canonical target owners. Mixed scalar/pack lockstep
patterns are supported when every referenced pack is bound and concrete.
Alias/non-type partial patterns still need grouped substitution and complete
canonical publication; supported nested primary-member expansions now retain
independent inner bindings. Keep those unsupported fields
fail-closed; the lockstep follow-up entry below records the remaining paths.

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
elements still need declaration identity in their live constexpr payloads.
Published namespace free functions retain their EntityId; an explicit
`FunctionPointerValue` keys that target and its declared pointer TypeId, with
canonical signature validation and noexcept relaxation. Propagate the selected
target through evaluation, structural serialization/rehydration, equality/hash,
and template argument binding before enabling live non-null function import.
Do not recover target identity from a spelling or ABI symbol.

Concrete implicit class-template specializations import as structural values,
including supported nonvirtual bases, nested members, and array elements. Array
members read the owning TypeId's published substituted subobject types.
Specialization-owner member
pointers recover member names/ordinals from the published primary pattern record,
verified by declaration pointer; the specialization TypeId remains the value
identity. Specialization field schemas retain substituted member types, offsets, bit-fields,
and flat union groups by owner TypeId. Missing field-layout and dependent base
facts still fail closed. The partial-specialization parser gap above and nested
anonymous-union hierarchy remain separate.

Non-type-parameter and alias partial patterns still need substitutable canonical
environments before dependent fields can import concretely. Direct concrete
type-pack occurrences now expand in data-member template-ids targeting namespace
templates or templates owned by non-template classes. Targets with an instantiated
class-template owner and nontrivial expansion patterns such as `Wrapper<Tail>...`
still need complete canonical publication. Callable pack expansion is separate.
Member-pointer graphs containing opaque non-type specialization arguments fail
closed: the canonical substitutor preserves those ExprIds and cannot replace a pattern argument such as `N` with
its concrete value. Publish substitutable parameter/value identity before
enabling those graphs; keep evaluator and overload-selection rules separate.

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
`test_canonical_structural_nttp_specialization_bases_ret0.cpp`,
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

## Deferred bases of selected-partial nested classes are not replayed

A nested class declared in a selected partial specialization now publishes
direct members, type aliases, and non-deferred base layout. A base whose type is
dependent on the partial's template parameters is parsed as a deferred base but
is not replayed on the selected instantiation, so inherited members from that
base are unavailable. Route deferred base replay through the full
instantiated-owner path rather than the lazy nested-type helper.

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

## Ordinary calls order concrete NTTP arguments by structural specificity

Ordinary call selection now scores concrete non-type template arguments in
parameter types above dependent patterns, so `choose(Cell<1u>*, T*)` wins over
`choose(Cell<N>*, T*)` for a `Cell<1u>*` argument.
`test_nttp_function_template_preference_ret0.cpp` pins this case. Two distinct
dependent NTTP patterns with the same structural shape still tie because the
ordinary path has not yet been replaced by the canonical
[temp.deduct.partial] comparison used by the function-address query.

## Expression parsing can overflow a 1 MB native stack before callable sema

The 16-level `test_deep_callable_chain_ret0.cpp` passes with the shipping Windows
compiler's existing stack reserve. A copy of that executable with its reserve
reduced to 1 MB also completes parsing and emits an object; member-call receiver
chains are now lowered through an explicit worklist instead of recursively
re-entering `AstToIr::generateMemberFunctionCallIr`. No stack reserve was
increased for this regression.
The native `Canonical adapter imports the source corpus at publication` fixture
still overflows at 1 MB, including in a pre-change binary. Reduce parser frames
and replace recursive syntax processing before expecting that corpus to fit.
The member-template pack owner regression still overflows at 1 MB, including
with a pre-change compiler binary. The instantiation worklist now collects
direct member class-template fields, so `Chain<N>` levels are pre-instantiated
rather than re-entering once per level; a 1.5 MB stack completes. A nested member
alias (`Holder<Tag<N>>::Box` -> `Owner<T>::List`) still instantiates recursively
inside each pre-instantiated `Chain`, so extending the worklist to member
template alias targets is the remaining step for 1 MB.

The nested NTTP-field regression
`test_nested_inner_value_pack_member_field_ret0.cpp` also overflows during
compilation with a 1 MB reserve. The shared pack scanner uses an explicit
worklist, but surrounding template materialization still needs smaller frames
and bounded scheduling. The nested type-pack field regression fits at 1 MB;
that result does not establish stack safety for the NTTP path.

The rare NTTP and callable payloads in `TemplateTypeArg` live behind immutable
32-bit handles in persistent arenas, and `TemplateArgumentVector` is now an
out-of-line owning list that can expose a span over its current storage. That
reduces `sizeof(TemplateTypeArg)` from 624 to 248 bytes,
`sizeof(TemplateArgumentVector)` from 2,528 to 24 bytes, and the clang `-O0`
`parse_primary_expression` frame from 343,560 to 250,504 bytes. The parser-only
harness regression and the full MSVC compiler copy at 1 MB both complete
parsing and emit an object for the deep callable-chain regression.

Payload handles are copy-on-write and never point at scratch storage. The
persistent handle arenas do not reclaim entries abandoned by rolled-back probes;
they are released only when the process exits. `TemplateArgumentList` and the
other out-of-line template/parser vectors use the same persistent arena, so a
span from one remains valid only until the next mutation. Compacting
`DependentQualifiedNameRecord` and the template-instantiation key reduces
`parse_type_specifier` from 160,376 to 59,336 bytes and
`try_instantiate_class_template` from 243,704 to 185,704 bytes in clang `-O0`
builds; the arenas do not reclaim old list arrays.

## Staged dependent alias expansions can lose pack elements

A nested alias chain that expands two dependent type packs into one member
template-id can materialize the wrong argument count. This remains deferred
with the nontrivial and multi-pack member-template expansion work.

## Callable member signature arguments can fail specialization matching

Callable member types also expose a separate matching discrepancy:
`template<class R, class... A> struct Packed { R (*value)(A...); };` compares
equal to the corresponding concrete function pointer through `__is_same`, but a
generic `Same<T, T>` partial-specialization trait reports false when passed
`decltype(Packed<long long>::value)` and `long long (*)()`. Both empty and full
packs reproduce; equivalent function-pointer aliases match correctly. Audit
member callable argument identity at partial-specialization deduction rather
than recognizing the trait spelling.

## Canonical substitution can form a pointer to reference

Canonical structural substitution also rebuilds `T*` with a reference argument
without applying the direct pointer constructor's reference-pointee rejection.
Keep this distinct from rollback of existing canonical rejection paths; the
producer needs a typed substitution failure for source-level immediate contexts
before enabling that rejection throughout the legacy callable pipeline.

## Pack substitution rollback and diagnostics remain incomplete

Effective primary owner bindings retain complete enclosing and local packs,
including empty and single-element packs. Shared iterative scanning distinguishes
current and inner expansions in member fields, namespace template targets,
deferred template-id bases, and NTTP fields. Nested inner packs can have a
different length from the outer pack, and repeated packs retain their full inner
binding alongside the selected outer element. Compile-time complex folds use the
pack referenced by their expression. Runtime function-parameter fold replay still
uses its separate legacy adapter.

Class partial-specialization deduction for `Box<Pair<Tail, Other>...>` now
retains both ordered packs, including empty packs, and checks repeated/scalar
deductions across expansion elements. The selected specialization publishes its
grouped environment and uses it for member fields and pack counts. The legacy
structural matcher still has its 64-level nested-argument guard. Alias and
variable-template substitution still have flattened
input adapters that need migration to grouped bindings; nested pack NTTP aliases
remain unsupported through those adapters. A nested member-template declaration
with a pack-expanded base such as `List : U...` is rejected by the parser; this
syntax gap is separate from supported template-id bases.

Computed NTTP packs stage their expressions and publish their IDs as one atomic
batch. A later element's hard error leaves no candidate expression-ID prefix.
This does not roll back required class instantiations: C++20 permits observable
instantiation effects before a later deduction failure and requires errors in
separately instantiated bodies to remain hard errors.

Dependent-name restamping now publishes canonical types only after semantic
substitution succeeds and the replacement stamp has been applied. Its canonical
transaction discards partially imported bindings when a later argument is
unsupported. Required class instantiations run before this transaction, so their
effects and hard errors remain independent of candidate restamping.

Pure callable-component substitution now owns its canonical import/substitution
transaction. Unsupported arguments and existing structural rejection paths
discard their provisional identities. Callable environment construction also
discards its imported prefix when a later argument is unsupported, before the
legacy signature walk begins. That walk independently publishes successfully
substituted components. Complete signature substitution can still retain earlier
component identities after a later failure, and nested callable signature
objects are shared by the legacy representation. Establish owned candidate
signature storage before extending rollback across that walk.

Other candidate-local type, legacy AST, registry/cache, and parser publication still
need an owned transaction boundary. `FrontendScratchTransaction` currently covers
scratch, declaration/template identity, canonical types, and symbol/namespace
publication; it does not make those other stores transactional. Do not wrap the
whole materialization path in it or erase required dependency instantiations.
Lockstep member and namespace-scope expansions now report
`TemplatePackLengthMismatch` (#1820) when referenced packs differ in length.
Expansion contexts that still use the flat legacy adapter remain dependent
instead of producing the diagnostic.

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

## Complex deep struct aggregate braces still use native parser recursion

The aggregate member-store emitter now traverses bases, members, default member
initializers, struct array elements, array shapes, initializer lists, and
zero-fill with explicit continuation frames and worklists. Array brace
initializers keep their dimensions on an explicit parser stack, and single-member
aggregate struct chains are parsed without native recursion. Struct braces with
multiple members, designated initializers, or non-aggregate/constructor paths
still recurse through `parse_brace_initializer`.

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

## Immediate-context function-pack substitution still accepts a missing member

A trailing return containing
`consume<T...>(static_cast<typename T::member>(0)...)` can retain a function
candidate when a later `T` has no `member`. With a variadic fallback overload,
`decltype(detect<Good, Bad>(0))` selects the invalid candidate instead of the
fallback. The reduced input is accepted by Clang C++20 as a valid SFINAE probe.
Audit function call-argument pack replay and immediate-context failure
classification separately from candidate expression-ID publication.

A separate required-instantiation observability probe using
`typename Writer<sizeof(T)>::type` as a default template argument is rejected
while parsing the template argument list. A reduced friend/ADL form reaches an
incorrect constructor diagnostic. These parser/lookup gaps prevent a source-level
friend-effect regression here; native tests retain a successfully instantiated
`Check<Good>` after a later class-body error without rolling back its cache entry.
