# Known Issues

This file is the authoritative inventory of unresolved gaps. Describe only the
remaining behavior and the next required change; completed work belongs in
`MIGRATION_PROGRESS.md`, which should link here instead of restating issue
details.

## AST-to-IR still resolves namespace-scope names by spelling

`IdentifierNode::resolved_name` is populated for static members and static
locals, but not for ordinary namespace-scope variables or functions. IR lowering
therefore re-runs unqualified lookup through `current_namespace_stack_`, and
detached static-storage initializer evaluation reconstructs a namespace context
from that stack. This is the Boundary 5 migration item: publish the resolved
declaration/entity identity on the identifier in sema, consume it in
AST-to-IR, and delete the IR lookup paths. Until then the IR namespace context is
a transitional bridge, not a second semantic authority.

## Operator candidate lookup still lacks complete member-name provenance

Hiding sets for forms that a requires-expression evaluates before class rebinding
remain unsupported; forcing that evaluation ordering overlaps Boundary 8A
class-template instantiation.

## Remaining dependent member-template field forms need concrete TypeInfo

Alias/non-type partial patterns still need grouped substitution and complete
canonical publication. Keep those unsupported fields fail-closed; the lockstep
follow-up entry below records the remaining paths.

## Built-in indirection of a callable copy is unsupported

`auto x = *function_pointer;` fails in IR conversion with
"Type with no runtime size reached codegen" instead of a source diagnostic, and
`int (*table[2])(int); auto& first = *table;` crashes. `auto& r = *function_pointer;`
works.

## Remaining structural NTTP shapes need canonical value import

Nested anonymous-union-in-anonymous-union hierarchy still needs an explicit
representation; flat contiguous layout groups alone cannot distinguish an
inactive outer union from an active outer variant containing an inactive inner
union. The parser currently flattens the inner group into the outer record and
can drop or duplicate members: `union { union { int first; int second; };
int direct; }` reports members `direct, second, direct`, so `Outer{}.first` is
reported as not found. Replace the flattening with an explicit nested union group
before adding any canonical representation.

Long double structural values are explicitly deferred until the backend gains
real long-double support. Do not extend structural identity carriers for a type
whose representation collapses to double elsewhere; keep these cases fail-closed
and track them with the broader long-double workstream.

Non-null object/function pointer targets, ambiguous or virtual-base member
pointers, and non-null object/function pointer array elements still need
declaration identity in their live constexpr payloads. Selected non-null function
target identity must survive evaluation, structural serialization/rehydration,
equality/hash, and template argument binding. Do not recover target identity from
a spelling or ABI symbol.

Object pointer arguments publish a target when the address-of operand is a bare
identifier, a namespace-qualified variable, a static data member of a
non-template class, or an element of a named array. A static member of a
class-template instance and a data-member address still lose identity, so two
distinct objects can instantiate the same specialization. The class-owned forms are not in the qualified
scope's symbol table; the class member list owns them, and publishing one needs the
class's declaration-builder entity as its owner. A non-template class declaration
provides that entity, but an instantiated class has no declaration node, so its
members have no owner to publish under until template instances carry one. That is
postponed to boundary 6 rather than solved here: the declaration builder keys a
class entity by owner, name and a signature that is always zero for classes, and an
`OwnerId` has no payload for a structural argument identity, so distinguishing two
instances of one template needs the structural-keyed registry work
`TemplateDeclId`-keyed owners belong to. Do not distinguish instances by spelling.
A function pointer member whose target returns a non-void type loses its identity
before the structural value exists: `constexpr Key key{&function, 1}` with an
`int (*)()` member records an integer zero instead of the function target, while a
`void (*)()` member keeps its published target and signature. Two such values
therefore compare equal as template arguments, and two distinct functions collapse
into the same one. This is producer-side, in the constexpr evaluation of a
non-void function address-of; canonical import cannot recover the target and must
not guess one from a name.

A member whose pointer has no publishable target makes structural extraction fail.
The parser can then attempt scalar construction from the same evaluation result;
whether an unsupported structural value can be accepted through that path needs a
reduced regression. Resolve the evaluator's own declaration origin into a published `EntityId` for every address-of
and subobject form; that carrier change belongs to constexpr evaluation and
template-argument construction, not to canonical import.

A structural argument initialized with a namespace-qualified function address,
such as `constexpr Key k{&ns::function, 1}` for a function-pointer member, can
leave a comparison of its specialization referring to an undefined `value` symbol
at link time. The equivalent unqualified function-pointer member is covered by
`test_structural_function_pointer_member_identity_ret0.cpp`.

Missing field-layout and dependent base facts still fail closed. The
partial-specialization parser gap above and nested anonymous-union hierarchy
remain separate.

Non-type-parameter and alias partial patterns still need substitutable canonical
environments before dependent fields can import concretely. Targets with an
instantiated class-template owner and nontrivial expansion patterns such as
`Wrapper<Tail>...` still need complete canonical publication. Callable pack
expansion is separate.
Member-pointer graphs containing opaque non-type specialization arguments fail
closed: the canonical substitutor preserves those ExprIds and cannot replace a pattern argument such as `N` with
its concrete value. Publish substitutable parameter/value identity before
enabling those graphs; keep evaluator and overload-selection rules separate.

Canonical import fails closed for object-pointer element offsets outside the
canonical 32-bit range or targets without a published identity. Some qualified,
data-member, and complex array-element origins still arrive without a target, as
recorded above.

## Partial-specialization constraint atoms must be proven before selection

An unknown atomic result is not proof of satisfaction, so a
specialization whose constraint uses a construct the evaluator cannot classify
is conservatively skipped; extend the canonical/constexpr atom path instead of
treating that atom as satisfied.

## Partial patterns without a canonical environment still resolve their record by spelling

A partial class-template pattern whose canonical pattern environment cannot be
published still resolves its `StructTypeInfo` through the synthesized `$pattern`
name plus a per-declaration ordinal. That suffix only makes the synthesized
spelling unique; it is not identity and must not be used by a new consumer. The
environment-keyed record is authoritative whenever an environment exists, and a
missing record for a published environment fails closed.
`partial_pattern_name_fallback` counts every remaining name lookup; extend
canonical pattern import coverage and then delete the fallback.

Remaining unsupported callable shapes are member-pointer callables and
ordered declarators with a non-pointer component (nested function, array,
reference, or member-pointer spine).
The fallback also remains for non-type-parameter argument shapes and for
patterns whose concept or requires-clause argument identity cannot be published.

The fallback remains for
an alias declaration whose target is or contains another alias-template-id (for
example `template<class T> using Q = P<T*>`) or a callable signature, for a
variadic alias parameter list, for an alias use whose argument count does not
match the alias declaration's parameter list, for alias arguments that are not
type arguments, and for an alias target whose shared template-parameter
substitution cannot produce a type specifier.

The class adapter still infers the enclosing-owner argument prefix by
subtracting the member template-id argument count from
`outer_template_args()` when a declaration does not record the boundary. Every
supported member-partial materialization records it explicitly; the inference is
kept only for declarations with no such producer and must be replaced when one
appears rather than extended with a spelling heuristic.

## Out-of-line member function template identity has unresolved shapes

The following shapes remain unmerged and fail closed: the definition keeps no
`TemplateDeclId`, and instantiation continues to attach through the in-class
declaration's first-use path.

- The out-of-line scanner does not parse a trailing `requires`-clause, so a
  declaration/definition pair constrained only by that clause cannot prove
  constraint equivalence. Concept constraints on the template parameters
  themselves are parsed and do participate.
- In the three-head form
  `template<class T> template<class U> template<class V> V Owner<T>::Box<U>::f(V)`
  the record stores the intervening member class's template head as its own
  parameters, so the function's own parameter shape is unavailable and the
  definition stays unmerged.
- An out-of-line member function template of a non-template class has no
  registered class-template pattern owner, so this resolution cannot reach its
  in-class declaration.
- The class-head check is lexical and conservative. A primary definition whose
  class head spells a parameter pack (`C<Ts...>`) declines identity, and
  partial-specialization heads such as `C<T*>`, `C<T&>`, `C<const T>`, and
  `C<T::type>` decline so they cannot adopt the primary owner's declaration.

Constrained redeclarations whose constraint formulas
`compareMemberTemplateConstraints` cannot build also fail closed and remain
distinct. Extending the formula builder is required before those shapes can
merge.

## Nominal import gaps are publication gaps, not importer gaps

`UserDefined` and `TypeAlias` indexes import as `UnmigratedNominal`. Instrumenting the
adapter over the whole suite shows why a category-dispatch fix cannot help: all 203
distinct `UserDefined` request shapes carry no published declaration `EntityId`, and
`TypeAlias` never reaches the adapter at all because the parser resolves
`using`/`typedef` uses to `Struct`/`Enum` before import. There is nothing for an
importer to read.

A class-template instantiation is the common `UserDefined` case, and its canonical
identity is a `TemplateSpecialization`, not a `Record`. Importing the pattern
declaration's `EntityId` would collapse distinct specializations into one, so these
shapes need a dedicated specialization publication path; that is boundary-6
structural-key work, not adapter wiring. Opaque indexes and unresolved dependent
names stay deferred for the same reason.

Do not add a nominal dispatch that reads an entity the producers never publish, and
do not pin it with a harness stub that fabricates one: a host test with a stubbed
`tryGetTypeInfo` proves the code compiles, not that any program can reach it.

## Expression parsing can overflow a 1 MB native stack

The nested NTTP-field regression
`test_nested_inner_value_pack_member_field_ret0.cpp` overflows during compilation
with a 1 MB reserve. The nested type-pack field regression fits at 1 MB; that
result does not establish stack safety for the NTTP path. Surrounding template
materialization still needs smaller frames and bounded scheduling.
Parenthesized `requires` operands in concept definitions can also overflow a
1 MB stack in expression parsing before constraint evaluation starts, for example
`Base<T> && (requires(T value) { value.probe(); } || __is_integral(T))`.
The remaining overflow above is in expression parsing, before constraint evaluation.

Persistent payload handles and out-of-line argument vectors are not reclaimed
when rolled-back probes abandon entries; they remain allocated until process
exit. Spans into these vectors remain valid only until their next mutation.

## Pack substitution rollback and diagnostics remain incomplete

Runtime function-parameter fold replay still uses its separate legacy adapter.
The legacy structural matcher still has its 64-level nested-argument guard.
Alias and variable-template substitution still have flattened input adapters
that need migration to grouped bindings; nested pack NTTP aliases remain
unsupported through those adapters. Remaining base-template-id gaps are tracked
separately.

C++20 permits observable class-instantiation effects before a later deduction
failure and requires errors in separately instantiated bodies to remain hard
errors. These required effects must not be rolled back with candidate-local
state.

Complete signature substitution can still retain earlier component identities
after a later failure, and nested callable signature objects are shared by the
legacy representation. Establish owned candidate signature storage before
extending rollback across that walk.

Owners outside those stores, such as a `QualifiedIdentifierNode` or a copied
`TemplateArgInfo` held by the AST/template registries, are not yet enumerable, so
in-place rewrites of those external owners remain outside the boundary. Legacy
AST nodes, parser publication, and direct `TypeInfo` field writes that bypass
the mutable accessor and mutator API still need an owned transaction boundary,
and production speculative parsing is not yet routed through it. Do not wrap the whole materialization path in the transaction or
erase required dependency instantiations. Expansion contexts that still use the
flat legacy adapter remain dependent
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

Member-template segments and member aliases whose targets are not directly
materialized class-template specializations still fall back to ordinary
instantiation. Stack usage for deep chains through those forms remains
unverified. Extend dependency scheduling to cover them while sharing base
substitution semantics with ordinary instantiation. Existing 1,024-level
coverage for concrete pack-expanded bases and nested-class chains does not
establish stack safety for these remaining forms.

## Legacy flat consumers cannot yet handle interleaved pointer/array declarators

Canonical `TypeId` identity is authoritative where imported, but unmigrated
consumers still read flat pointer/array projections. Member layout and constexpr
member sizing now consume the ordered declarator spine, so an interleaved member
such as `int (*cells[3])[4]` keeps pointer-sized elements and pointer alignment
instead of the flat base element size. Constexpr member typing also prefers the
published member `TypeId` when one exists.

Local array objects now allocate their full ordered element count and a
reference to such an array holds the array address instead of dereferencing it
as a value. Value `auto`, `auto&`, and `auto*` deduction preserve the ordered
spine, including array decay and address-of. Identifier lowering asks sema's
published canonical `TypeId` whether the type denotes an array instead of
re-reading ordered declarator syntax. Instantiated reference-to-array parameter
bounds are materialized onto the type specifier before that canonical query.
Array-member lvalue-address lowering now emits the member address for every
array member, so direct member-array reference arguments receive a real address
instead of an unemitted temporary. Conditional array branches now consume their
annotated array-to-pointer conversion without dereferencing the materialized
address again. Subscript lowering distinguishes a pointer-to-array declarator
from an array object even when the declaration retains array extents for its
pointee. Builtin unary `*` consumes sema's canonical `[conv.array]` plus
`[expr.unary.op]` result type, so an interleaved array selects a
pointer-to-array element instead of the flat base type. Pointer arithmetic
decays array operands to their ordered element type and scales offsets by the
canonical pointee size. Identifier and qualified-global array detection now
consults the sema canonical declaration/type predicates instead of the flat or
ordered projection. Other remaining flat readers still fail closed for
non-projectable shapes;
remove this entry when those consumers migrate and the compatibility projection
fields are deleted.

## Ordered-declarator size fails closed when the canonical import is unmigrated

An ordered declarator's array extents come from the canonical array shape, not
from `DeclaratorComponent::payload`. Two bases still import as
`UnmigratedNominal`, so an ordered operand with an array outside its outermost
pointer has no canonical object size:

- a template-parameter element in a pattern, such as `T (*[2])[4]` in
  `test_template_pattern_interleaved_declarator_ret0.cpp`; a dependent sizeof
  operand is deferred, so this is a gap only for a consumer that reaches the
  pattern type without substituting it first.
- an alias to a nested class of a class-template specialization, such as
  `using Inner = Outer<int>::Inner; sizeof(Inner (*[2])[3])`; the type-id form
  fails closed with `IncompleteSizeofOperand` instead of reporting the 16-byte
  array of two pointers.

Needed: publish the nominal entity for these bases so the canonical array
import can size them. Until then the query returns 0 and the sizeof-operand
rule rejects the operand rather than reading the spine payload.

## Omitted array bounds live on four legacy template-argument representations

`sizeof(T)` and `requires { sizeof(T); }` in a function or class template reject
`long long[][3]` and `int[]` instead of reporting the inner or element extent,
and `sizeof(identifier)` of a declaration with an omitted bound reaches the same
canonical `Array(unknown, Array(N, T))` shape in codegen and constant
evaluation. A misplaced omitted bound is diagnosed where it is written in
declarators, type-ids, and template arguments, so it cannot collapse to `T[]`
and surface later as an unknown-bound `sizeof`.

The remaining gap is representational: `TemplateTypeArg`, `TemplateArgInfo`,
`TypeIndexArg`, and `ResolvedAliasTypeInfo` each carry
`has_unsized_outer_array_dimension` beside the known inner extents. Boundary 6
replaces them with the canonical template-argument identity, and until then
every new representation of a template argument must carry the omitted-bound
bit or `sizeof` and requirement validity will disagree again.

## Alias template-parameter facts are projected per materialization site

`resolveAliasTypeInfo` keeps a target such as `using A = T[5]` as the structural
`TemplateDeclId` and parameter index plus the target spelling. The spelling is
only a lookup key for `ExpressionSubstitutor`'s name-keyed substitution map and
the `has_template_parameter_identity()` gate; declaration identity is the
`TemplateDeclId` and index pair.

Three sites materialize a specifier from that resolution and must project the
facts: `resolveTypeInfoToTypeSpec`, `applyResolvedAliasModifiers` in
`ExpressionSubstitutor.cpp`, and the typedef branch of `parse_type_specifier`. A
new site that builds a specifier from a resolution can omit the projection and
silently fall back to the placeholder row's spelling, so the invariant is that a
resolution result should project itself when it is materialized.

The projection throws when its target already carries a template-specialization
or dependent-name stamp, because `set_template_parameter_decl` clears both. A
caller that legitimately needs to apply it to a stamped specifier needs an
apply-without-clearing path instead of relying on the current assertion.

## Flat pointer depth still differs from the canonical pointer shape

`canonicalPointerShape` in `CanonicalShapeQueries.h` is the single answer for
pointer presence, pointer depth, and per-level cv: one walk reports `depth`,
`member_pointer`, `function_pointer`, and the cv of every level, ordered
outermost-first. `canonicalIsPointer`, `canonicalPointerLevelCount`, and
`canonicalCopyPointerLevelCvs` delegate to it, so the three facts cannot
disagree, and `projectCanonicalDerivedShape` writes it into the flat
innermost-first order. The canonical structural `__is_pointer` answer also
reads it.

The compatibility projection still encodes two shapes differently, so its raw
depth is not a substitute for the query:

- A function pointer is one canonical `Pointer`. The flat syntax stores an
  implicit `FunctionPointer` category (flat depth 0) or an explicit level on a
  `Function` category (flat depth 1); both import to depth 1.
- A member pointer is canonical depth 0 with `member_pointer` set and no
  object-pointer presence, while the flat declarator still stores one pointer
  level plus the member owner. A pointer to a member pointer is canonical depth
  1 with `member_pointer` set.

Measured over the source suite at the sampled import sites: 22,746 Supported
imports, 153 whose canonical depth differs from `runtime_pointer_depth()` (72
spell a callable with the function-pointer category, 64 are member pointers,
and 17 are other shapes whose flat runtime depth and canonical chain differ),
and 0 per-level cv differences where the depth matched. Consumers that need
presence, depth, or cv read `canonicalPointerShape` directly; the compatibility
projection is written from that shape in the flat order. Other flat readers
still fail closed for non-projectable shapes; remove this entry when those
consumers migrate and the compatibility projection fields are deleted.

## Opaque type-identity terminals still fall back to `TypeIndex`

`equalTypeSpecifierIdentityImpl` and `hashTypeSpecifierIdentityImpl` now share
one canonical structure: outer-first declarator components (alias-composed, with
per-level pointer cv and array extents including an unknown bound) plus a
terminal identity. The terminal is a builtin kind, a published record/enum
`EntityId`, a callable signature, or a class-template specialization. The pair
therefore merges a typedef with its underlying type and keeps distinct
specializations and per-level cv distinct, and it stays publication-independent
because no canonical `TypeId` is compared or hashed and no nominal layout is
required.

Shapes with no canonical terminal and a valid `TypeIndex` still compare and hash
by that index: 146 `UserDefined` indexes (109 unresolved template-parameter
names without a `TemplateDeclId` and 37 other opaque indexes), 40 structs with a
type index and no entity, 29 pre-deduction `Auto`, 11 structs whose entity
cannot take over while the other side stays unpublished, and the injected class
whose entity is shared across specializations
(`test_injected_identity_ctor_dtor_expressions_ret0.cpp`). A shared placeholder
would merge distinct types, so those pairs keep that fallback until identity is
published for them. An opaque terminal with an invalid `TypeIndex` is never
identified by the placeholder: it uses the template-parameter declaration/index
or the normalized spelling, and fails closed when neither exists, so distinct
unresolved names and placeholders cannot silently merge. A member pointer with
no published owner likewise uses its class name or fails closed, and a callable
terminal without any signature is a hard error.

## Derived pointee and element readers are only partly canonical

Suite measurement of readers that peel `pointer_levels()` or `array_dimensions()`
to a pointee, element, subscript result, or dereference result:

- `arrayElementType`'s flat array peel: 51 calls, all Supported, and the
  canonical element matched the flat peel. That peel now uses
  `requireCanonicalShape` and projects the canonical element back. The
  flat-reader count in that file went from 4 to 1; one `array_dimensions()`
  emptiness check remains.
- Range-for over an array: 12 calls, all Supported, and the canonical element
  matched. That peel projects the canonical element and keeps `is_array` set
  when the last bound is dropped, which is what `remove_outermost_array_dimension`
  did. It still reads `array_dimensions()` to choose the empty-bound branch.
  `test_derived_array_row_ret100.cpp` depends on the remaining row bound.
- Indirect-call dereference: 4 calls, 1 Supported match and 3 `UnmigratedNominal`
  struct pointers at flat depth 1. It stays on the flat pointer level.
- Arrow pseudo-destructor: the suite sample was Supported, but a struct pointer
  that main accepts imports as `UnmigratedNominal`, and requiring the canonical
  pointee throws. It stays on the flat pointer level.
- Member-access dereference and the flat member-pointer pointee fallback had no
  suite hits, so they stay on the flat peel.
- QualLookup's member-pointer result: 65 calls, 61 Supported with no canonical
  pointee because a member pointer is not a canonical Pointer, and 4
  `UnmigratedCallable`. It stays on the flat levels.

## Flat pointer and array fields are still required

The flat pointer-level and array-dimension fields cannot be removed yet. Each
remaining reader exists for one of the reasons below, and the entries say what
would remove it and when that work is due. Until an entry's condition is met the
fields stay, and the per-file reader inventory must keep ratcheting down.

`ConstExprEvaluator.h` has no reader left: same-type constexpr object reuse now
decides from `canonicalTypeSpecifierIdentityEqual` over the two object types, so
a typedef spelling of one record shares while a different record, a per-level
pointer cv difference, or a different array extent does not.

`ConstExprEvaluator_Core.cpp` keeps thirteen readers in three groups:

- **Result-shape peels** in `tryGetConstexprSubscriptResultType`, pointer-to-member
  arrow access, and unary `*`. They are the source of the flat
  `EvalResult.exact_type` that the rest of the evaluator still consumes through
  `set_array_dimensions` and `remove_pointer_level`, so they cannot be migrated
  on their own. Needed: the boundary 12 constexpr value model, whose `EvalResult`
  carries the canonical `TypeId` and drops the flat exact type. Do them then, not
  before. A `requireCanonicalShape` peel is not a substitute today either: local
  nominal records import as `UnmigratedNominal`, so a canonical pointee or
  element would throw for `test_constexpr_ptr_arrow_local_struct_ret0.cpp`,
  `test_constexpr_ptr_member_call_ret0.cpp`, and
  `test_constexpr_lambda_copy_state_ret0.cpp` until those entities are
  published.
- **Structural descriptor writers**: `makeStructuralArrayValueIdentityImpl` and
  `make_member_type_spec` copy `StructMember` descriptor fields into the
  structural value and type-spec payloads. Needed: the boundary 12 structural
  value carriers; these are writers for consumers that still read those
  descriptors, so they move with the value model.
- **`sizeof` over a class member** reads `DeclarationNode::array_dimensions()`,
  which holds bound expressions rather than the type projection; the reader gate
  counts it because the access is spelled through `->`. Nothing migrates here.
  The eventual fix is for the gate to tell bound expressions apart from type
  shape, not a code change in this path.

`Parser_Templates_Function.cpp` keeps three readers that copy
`TypeInfo::TemplateArgInfo` flat fields into a `TemplateTypeArg` for the legacy
`TemplateRegistry` specialization lookup. Needed: the boundary 6 template
argument identity and the deletion of the flat `TemplateArgInfo`/`TemplateTypeArg`
projection; until then these are writers for a consumer that is not shape-based.

The inventory is 806 reads across 67 files.

## static_cast qualification covers references and composite components, not general conversion validity

`static_cast` now validates qualification at one cast site for every
qualification-decomposition shape. A reference target compares the bound
referent with [conv.qual]/3 through [dcl.init.ref]/4 reference-compatibility,
applies reference collapsing, and additionally enforces the [dcl.init.ref]/5.2
rule that only a const, non-volatile lvalue reference binds an rvalue. Array
components carry their element cv and bound, including the [conv.qual]/1 rule
that an array's cv is its element's cv and the [conv.qual]/3.2 unknown-bound
result. Pointer-to-member components carry their class owner and member-type cv;
[conv.qual]/1 and /4 put pointer-to-member in the same qualification rule as an
object pointer, so no separate member-pointer rule applies. The identity shape
remains the fallback for `UnmigratedNominal`.

The check classifies only casts whose source and target terminals are the same
type and whose components are similar. A `static_cast` that changes an array
bound between two known bounds, binds a reference to a non-similar referent
(for example `int**` to `const int* const* const* const&`), or converts between
unrelated pointer-like types is still accepted because no pass validates the
remaining conversions ([conv.ptr], [conv.mem], array-bound similarity, or a
user-defined conversion sequence).

Functional-cast spelling is still classified as a static_cast conversion even
though [expr.type.conv]/1.1 makes a single-expression functional cast
equivalent to the corresponding C-style cast, which may perform a const_cast.

## Decltype operator overload result typing is parser-owned

`get_overload_return_type` in `Parser_Expr_QualLookup.cpp` still derives a binary
operator's result type by reading the resolved member or free declaration's
return type and resolving self-references. The direct re-resolution workaround is
gone now that canonical operator lookup handles specialized aliases, but the
proper owner remains semantics (boundary 4): sema should publish the resolved
expression type and the parser should only consume it.

## User-defined conversion identity gaps

A user-defined conversion sequence is ranked by its second standard conversion
sequence only when the plan carries the selected conversion function or
constructor identity. Member functions publish no declaration `EntityId`, so a
conversion operator is identified by its declaring class `TypeId` plus its
member-function ordinal, and a conversion function template specialization by an
interned `TypeId` key over its published `TemplateDeclId`, the declaring class's
canonical specialization when the member pattern was cloned, and canonical
deduced template arguments. The key is interned, so equal keys mean an equal
declaration and argument list; it is not a spelling, source location, raw
address, or hash-only comparison. A lambda-closure conversion (declared on the
closure or synthesized for it) and a constructor whose class publishes no schema
entry for the selected declaration (or no declaration at all) publish no such
identity. A conversion function template specialization whose deduced argument
has no canonical identity (a pack, a dependent argument, a template-template
argument, or a non-type value the canonical importer does not represent) also
publishes no key. A sequence without identity keeps no standard tail and stays
unordered, which is the conservative answer; ranking it needs a published
numeric identity for the selected declaration.

The conversion function template key includes the declaring class's canonical
specialization when that class is a class-template instance. Member function
templates cloned into different specializations of a class template keep the
pattern's published `TemplateDeclId`, so the owner distinguishes the clones: two
bases that are different specializations of one class template with identical
deduced member arguments publish different keys and stay unordered instead of
being ranked by their second standard sequences. A plain class declares each
member template once, so the member declaration alone still keys it. When the
declaring class is a class-template instance but its canonical specialization
cannot be published, the selection publishes no key and stays unordered rather
than falling back to a spelling or to a sibling specialization's key.

The member-function ordinal and the constructor schema index are within-snapshot
keys, not durable identity: they disambiguate selections inside one published
schema snapshot and do not survive a schema republication that reorders entries.
Both have to move to the declaration or entity identity once constructors and
member functions publish one.

The compatibility declaration bridge now reports the canonical owner `TypeId` and
schema index of the constructor it selected, so one constructor has one identity
whichever route selected it. The mapping needs the class's published constructor
schema and a declaration that lists the selected constructor; a constructor that
exists only in the compatibility `StructTypeInfo` (for example an implicit
special member synthesized without a declaration entry), or a class whose
constructor schema or declaration is unpublished, still keeps no identity and
stays unordered. Those selections need a published declaration or entity identity
before they can be ranked.

## Remaining conversion function template cases

- Resolve any remaining deferred base shapes that have not produced a concrete
  record during class instantiation before they contribute conversion candidates.
  Non-deferred aliases that name a concrete record now resolve; deferred bases
  without a concrete record must stay excluded.
- Seed built-in subscript candidates from conversion templates whose return
  pointer or array-reference shape still depends on the template parameters.

## Out-of-line static data member definition edge cases

Two boundaries remain: a repeated out-of-line definition is accepted as another
attachment instead of producing a redefinition diagnostic, and the class-template
conflict check covers concrete arithmetic member types; a dependent element type
is validated against the instantiated declaration.

An assignment through an unparenthesized qualified chain that names a member
class template or a nested owner (`Outer<T>::Inner<U>::data = 1;`) is currently
lowered as a discarded value expression rather than an lvalue store, so the
write has no effect; parenthesizing the chain keeps it an lvalue. This belongs
to the statement-versus-declaration qualified-name disambiguation, not to the
out-of-line definition itself.

The out-of-line declarator-id carries the owner's class `OwnerId`, but the
legacy type registry is still spelling-keyed. The owner spelling therefore
remains on the declaration as the lookup key, and the carried identity is only
cross-checked against the spelling resolution. The registry must answer by
`OwnerId`/`EntityId` before the spelling can be removed. A template-id owner
whose resolved struct declaration has no published entity (for example an
explicitly specialized class template) carries no identity and stays on the
spelling path.

## NRVO coverage and exception lifetime

The safe NRVO slice covers top-level named locals in lambda call operators and
regular functions with constructor-style initialization and one top-level
identifier return. A candidate is rejected when any other return can execute
after its declaration, because that exit would have to construct a different
object over the live local. Returns that precede the declaration, throws, and
returns inside nested lambdas or local classes do not prevent elision; returns
inside SEH `__try`/`__except`/`__finally` bodies belong to this function and are
included in the scan. The following candidate shapes remain unsupported:
nested-block locals, multiple top-level return statements, conversion-operator
initialization, and copy-initialization from a function call.

Hidden-return calls through free functions, static members, ordinary member
functions, virtual dispatch, function pointers, function-pointer data members,
and member-function pointers now construct directly in the caller destination
in both return expressions and local initializers; no temporary return slot or
caller-side copy is emitted. Conditional receivers forward the destination to
the selected branch call. Only the expression that constructs the result
receives the destination, so receivers and arguments still construct into their
own storage. Destination matching ignores top-level cv, so `const T x = make();`
still elides even when `T`'s copy constructor is deleted. Constructor-initialized
and call-initialized struct locals with destructors reserve a construction flag
in the prologue, set it only after initialization completes, and clear it after
destruction. Exception cleanup checks that flag, so an incomplete callee local
or caller destination is not destroyed and a later declaration in the same
frame cannot observe a stale flag. A result constructed into the hidden return
slot is also registered as a function-scope cleanup action with its own
completion flag, so a later destructor or finally action that throws destroys
the completed result exactly once. Remaining lifetime gaps are initialization
paths that bypass the tracked struct-initialization tail, such as some array and
member-aggregate forms. Small aggregates returned in registers still initialize
through the ABI result temporary, so copy-initialization from such a call is not
elided yet and still requires an accessible copy or move constructor.

## Remaining namespace-scope initializer lowering gaps

Remaining gaps:

- A namespace-scope reference initializer that needs a runtime pointer value
  (for example `int& r = *p;`, a reference-returning call, or a reference to a
  reference member or bit-field) cannot be expressed as a static relocation and
  fails closed with `StaticReferenceInitializerRequiresConstantAddress` (#1518).
  Binding a reference to a prvalue is materialized only for a scalar constant.
- There is no dynamic global initializer for a reference whose referent address
  is produced by a conversion function. The reference- and value-returning forms
  (`int& r = A();` and `const int& r = A();` with `operator int&()` or
  `operator int()`) are diagnosed with
  `StaticReferenceInitializerRequiresConstantAddress` (#1518) instead of
  materializing a zero-valued temporary. Local references and namespace-scope
  references to statically allocated objects keep their existing lowering.
- Namespace-scope `using`-directives are not consulted during detached
  evaluation.
- Inline and nested namespace variables can be emitted with a name that does not
  match their reference (`undefined reference to 'N::a'`), which is a separate
  symbol-emission gap.

## Constexpr member access requires an unqualified object spelling

An object expression spelled as a qualified name, such as
`Holder::constant.first`, reaches the generic "Complex member access expressions
not yet supported" path even when the member is a direct member of the object's
own type. Member lookup resolves identifier, nested member access, call result,
and array element object expressions. Needed: resolve a `QualifiedIdentifierNode`
object source and feed its materialized object into the same member lookup.

## Constexpr aggregate reference parameters lose namespace-scope arguments

A constexpr function with an aggregate reference parameter reports
"Dangling reference binding in constant expression" when the argument is a
namespace-scope constexpr object, before member lookup runs; the same function
called with a local object works. The argument binding, not base subobject
member lookup, needs to keep the namespace-scope object's identity.

## Subscript storage follow-up architecture

Runtime storage now describes what each temporary holds, but two consumers
still infer semantics from it. `ReturnOp` should carry explicit by-value versus
reference intent instead of consulting per-function `TempVar` lvalue metadata;
that is also where direct SysV 9-16-byte aggregate returns from an address need
ABI-aware copying. Subscript lowering should pass an explicit selected-object
kind (array versus scalar) instead of treating `ContainsAddress` as "array
result". `ArrayStore` and `ArrayElementAddress` still need an explicit element
shape so codegen stops classifying pointer-valued elements as records from
`TypeCategory` alone.

## Remaining flat trait consumers

The sema-owned construction traits
(`__is_constructible`, `__is_trivially_constructible`, and
`__is_nothrow_constructible`) answer exact matches, supported scalar and
reference conversions, same-class by-value arguments, publicly derived by-value
arguments whose base publishes a usable public copy or move constructor,
unrelated class arguments with exactly one viable non-explicit public converting
constructor, and omitted default arguments from published constructor schemas.
Direct conversion operators now rank by the standard conversion from their
result to the parameter; the best inaccessible or deleted operator makes the
conversion ill-formed, and explicit operators stay out of implicit argument
conversions. Operators declared on non-virtual bases participate in the same
ranking, while virtual or deferred bases, multiple or undecidable converting
candidates, derived bases whose copy/move facts are unpublished or inaccessible,
incomplete schemas, and unresolved canonical types still defer. Non-public
constructor candidates now participate in canonical overload ranking, so an
inaccessible better match makes the queried construction ill-formed instead of
letting a worse public candidate decide. Deleted converting constructors are
published with parameter schemas for non-template classes, so a deleted better
match rejects construction. Primary and partial class-template instantiations
clone and substitute deleted general constructor parameters, including dependent
parameter types, so the selected specialization publishes rankable deleted
candidates.
Constructor schemas now retain ellipsis and rank
extra arguments through the ellipsis conversion sequence, including class types.
Passing a class through `...` is conditionally supported, so constructibility
adds no copy/move requirement and the selected constructor's exception
specification decides the nothrow answer. Dependent extra arguments still defer.
Runtime constructor lowering still emits a fixed-arity signature and lacks
ellipsis promotions and ABI handling. General constructor overload resolution
therefore retains its previous arity restrictions; canonical trait support does
not enable runtime calls with extra arguments.
Inherited variadic constructors retain ellipsis in their candidate metadata, but
their synthesized base initializer forwards only named parameters. Runtime
forwarding of unnamed arguments remains unimplemented; the constructibility
regressions query the overloads without invoking those wrappers.
Constructor-template deduction now participates in the constructibility traits
when every non-defaulted template parameter is deduced directly from a fixed
parameter and remaining variadic arguments are ranked through the ellipsis
conversion sequence, including class types. Constrained constructors and
non-direct dependent parameter patterns still defer. An unspecialized constructor
template is not published as an ordinary canonical constructor. Class-template
constructors whose owner arguments are already substituted are covered by the
new schemas.
The canonical nothrow query proves arithmetic literal defaults (including unary
signs) from a published trailing suffix and rejects omitted defaults whose call,
throw, new, resolved operator, member-access, subscript, ternary, or
initializer-list expression is known to throw by recursing through its operands
and elements. It also rejects a class-typed default initialized from a
numeric/bool literal when one canonical constructor matches exactly and is not
noexcept. Arithmetic/bool literal defaults bound to references and null pointer
literals bound to pointer defaults are also proven nothrow. Class-typed defaults
initialized by a constructor call with scalar literal arguments now publish
nothrow or known-throwing facts from the unique canonical constructor candidate;
omitted nested defaults consume the nested constructor's published
nothrow/known-throwing facts. Canonical record facts now publish
`NothrowDestructible`. Defaults initialized by a call to a known noexcept
function with scalar literal arguments are also proven nothrow. A nested
constructor call whose parameter is a class now proves nothrow when one scalar
literal reaches it through the unique public non-explicit `noexcept` converting
constructor; constructors with user-provided struct parameters use the canonical
class fallback when the direct parameter import is unavailable. A known-throwing
scalar-to-class conversion now produces a known-throwing default instead of
deferring. Deeper class conversions, omitted nested defaults without published
exception facts, non-literal call arguments, reference defaults requiring general
conversion analysis, and unresolved operator selections still need initialization
and exception facts; those shapes continue to defer until their canonical facts
are published.
Concepts using unsupported sema-owned traits must retain an explicit unknown outcome rather than treating
it as satisfaction or failure. Static member semantic identity must remain
canonical through any remaining flat-consumer migration.

## Production speculative parsing is not yet integrated with frontend scratch transactions

Production parsing does not yet route tentative declaration work through
`FrontendScratchTransaction`. A live `Parser` binds one append-only root
publication journal to its `FrontendContext`: transactions restore lexer/token
state, top-level roots and instantiated tags, pending-root additions, and
discarded-root handles before global AST storage truncates. Front insertion,
pre-checkpoint root erasure, and pending-root drain/clear fail closed. The same
checkpoint restores the member-function and struct parse-context stack tails,
the class-template pack and lambda-capture stack tails, pending explicit
template arguments, the current function/linkage, and the last calling
convention; commit requires them to return to the checkpoint. The corresponding
guards pop only frames they pushed after rollback truncation.
Pre-existing `QualifiedIdentifierNode` dependent-name and template-argument
rewrites are now snapshotted per transaction segment and restored with rollback.
Pre-existing AST declaration owners likewise snapshot their outer template
parameter names, copied `TemplateArgInfo` arguments, and environment snapshot
before `set_outer_template_bindings` rewrites them. Pre-existing
`StructDeclarationNode` owners snapshot the whole declaration before member,
base, nested-class, body-state, or identity rewrites and restore it before AST
storage truncates. `FunctionDeclarationNode` owners snapshot parameters,
referenced declaration metadata, definition, lookup context, mangled name, NTTP
arguments, body state, and flags. Constructor and destructor declaration owners
snapshot parameters, initializers, definitions, owning types, template and body
metadata, flags, mangled names, and lazy registry keys. Standalone
`DeclarationNode`, `EnumDeclarationNode`, and `TypedefDeclarationNode` owners
snapshot declaration, array, alignment, name, scope, enumerator, underlying-type,
and identity rewrites. Namespace declaration owners journal their appended
declaration count. Mutable expression and statement owners share a type-erased
snapshot journal for `IdentifierNode`, `BlockNode`, `InitializerListNode`,
`TemplateParameterNode`, `CallExprNode`, `ConstructorCallNode`,
`RangedForStatementNode`, `TryStatementNode`, `BinaryOperatorNode`,
`UnaryOperatorNode`, and `IfStatementNode`; rollback restores and releases each
snapshot before AST truncation. Template function and class declaration owners
snapshot parameters, declaration IDs, and deferred bodies. Template alias and variable declarations snapshot bounds, replay
metadata, and declaration IDs; standalone variable declarations snapshot their
initializer, storage/linkage/constexpr flags, and outer bindings; friend
declarations snapshot class, template, function, type, and declaration identity
fields. Pre-existing `TypeSpecifierNode` owners snapshot the whole type shape
before size, cv/pointer/array, declarator, specialization, dependent-name,
member-class, or identity rewrites.
All allow-listed AST owner classes now expose their mutations through journaled
accessors. Direct `TypeSpecifierNode` mutators and non-const type-specifier view
accessors journal the type or its owning node before mutation. The direct-write
audit now finds no unjournaled mutators among allow-listed AST owners. Existing
`TypeInfo` rows reached
through the mutable accessor/mutator API are snapshotted and restored with their
cold records, and legacy AST nodes allocated after the mark are truncated.

Diagnostic text and note backing storage is still retained
for borrowed engine-lifetime diagnostic views. Reclaiming speculative payloads
requires a bounded lifetime contract for those views; record rollback alone
cannot release that storage safely.

Namespace and legacy type registries remain process-global, while symbol journals
are enlisted per `FrontendContext`. Ordinary writes from another context are not
isolated: rolling back the owning context can undo global registry writes whose
symbol-table writes belong to another. Keep publication work within one context
until registry ownership or journal enlistment moves to a shared coordinator.

## Bound symbol tables cannot publish independent scope topologies

All tables bound to one `FrontendContext` share its scope ID sequence and cursor.
If one table creates scopes, another table still at the root cannot append its
own topology: its next local scope ID conflicts with the context arena. Keep one
scope-producing table per context until scopes and cursors have independent
ownership.

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
- Direct conversion of a member-function pointer from a base owner to a derived
  owner is accepted but does not encode the `this` adjustment; calls through the
  converted pointer use the unadjusted receiver.
- Some member-template address overload shapes can still route instantiation
  through a parser-owned root. `ensureMemberFunctionAddressMaterialized` logs a
  warning and leaves the address unresolved in that case rather than failing
  the translation unit, so the old uninitialized-slot behavior can still appear
  there.

## typeid results cannot be bound to a type_info reference

`const void* p = typeid(T);` works, but binding the result to a reference such
as `const std::type_info& info = typeid(int);` makes the reference denote the
pointer variable instead of the pointed-to object. A later `info.name()` call
passes the address of that stack slot to the member function. Use the pointer
form until reference binding of the typeid result is fixed.

## Some member-pointer spellings lose type identity before RTTI emission

For `template<class T> struct Owner { struct Inner {}; };`, a member-pointer
alias written as `using P = int OwnerAlias::Inner::*;` with
`using OwnerAlias = Owner<int>;` can fail with `member-pointer substitution has
no published owner identity`. The direct type-id `int Owner<int>::Inner::*`
can reach `typeid` with only the identity of `int`. Introduce
`using InnerType = OwnerAlias::Inner;` and use `int InnerType::*` to bind the
nested class specialization.

An alias such as `using M = int (OwnerAlias::*)(); using P = M*;` can also
drop the outer pointer in `typeid(P)`. The parenthesized declarator
`int (OwnerAlias::**)()` preserves it.

## Offset-zero data-member-pointer alias casts fold to the null sentinel

Direct initialization from the address form works for a data-member-pointer
alias: `using PM = int C::*; PM p = &C::first;` with `first` at offset zero keeps
a non-null pointer-to-member. The explicit cast spellings do not. Both
`PM p = static_cast<PM>(&C::first);` and the functional form
`PM p = PM(&C::first);` produce the all-bits-one null sentinel, so `p == nullptr`
is true even though `&C::first` designates a valid member. A nonzero-offset
member and a direct `int C::*p = static_cast<int C::*>(&C::first);` are
unaffected. The alias target specifier arrives with the flat terminal category,
one runtime pointer level, and its class owner rather than the
`MemberObjectPointer` category, so the cast does not take the same-type path and
the offset-zero value is reclassified as null. Making the flat alias shape reach
the explicit-cast lowering as a member-object pointer is the remaining fix.

## Static-storage aggregate members of data-member-pointer type zero-fill to a null offset

Direct null member-pointer initialization now stores the all-bits-one sentinel:
declarations and value-initialization, assignment, return, argument passing,
`static_cast`/`reinterpret_cast`, and conditional operands all produce it, and
equality/inequality, bool conversion, and base-subobject adjustment agree with
it. `&D::first` (offset zero) therefore compares as a valid non-null
pointer-to-member. Zero-initialized aggregate subobjects of that type still
write zero bytes, which is a valid offset-zero member pointer rather than the
null sentinel: a namespace-scope `Holder g{};` reads its member pointer back as
non-null, and a zero-initialized array of data-member pointers has zero
elements. Local struct aggregates and implicitly generated default constructors
do write the sentinel.

## Local plain pointer operands of `.*`/`->*` can escape the operand check

`object.*operand` and `pointer->*operand` now report
`InvalidDataMemberPointerOperand` at the operator for a right operand that is
conclusively not a pointer-to-member: `int`, `double`, `nullptr`, an enum, a
class without a conversion, a function pointer, or a plain-pointer parameter.
A plain-pointer *local* remains deferred: dependent member-pointer return
substitution can collapse `T C::*` to a plain pointer before sema sees
the call result, and the legacy lowering peels that pointer back to the member,
so a local that is genuinely `int*` is not distinguishable from the collapsed
form at this boundary. Member-pointer owner identity itself is carried by one
substitution rule; see the projection notes below.

## Dependent member-pointer owners the shared nested-name-specifier parse cannot resolve

Member-pointer owners parse through one nested-name-specifier block, which
also resolves a `decltype-specifier ::` component from its deduced class and
stamps a template-id component's dependent specialization identity. Two
dependent spellings still cannot name an owner:

- `decltype-specifier ::` needs a concrete, published deduced type. A dependent
  operand such as `template<class T> int read(T& object, int decltype(object)::*member)`
  is rejected while the nested-name-specifier is built and reports `Expected
  identifier token`. The next change is to retain a dependent owner record on
  `MemberPointerOwnerParse` and resolve it during substitution.
- A dependent qualified member-template owner parses in a parameter
  (`Outer<T>::template Inner<U>::*`), but a `object.*member` access through that
  parameter still reports `InvalidDataMemberPointerOperand` because the owner is
  not substituted before the operand check. The same owner in a namespace-scope
  variable template is claimed by the out-of-line member-definition probe and
  reports `Expected type specifier`.

## Static-member template initializer replay still handles unresolved lookups

All four static-member initializer sites (in-class primary and partial
specialization, out-of-line clones in
`Parser_Templates_Inst_ClassTemplate.cpp`, and the lazy clone in
`Parser_Templates_Lazy.cpp`) attempt structural AST substitution before lexer
replay and then classify the structural result as bound, deferred, or an error.
A bound result is accepted without replay, including a bound indirect call whose
callee has no direct `FunctionDeclarationNode`. A genuinely deferred result
keeps replay available for the definition-context lookup state, the pack-aware
`Parser::ReplayTemplateBindings` bindings, and point-of-instantiation
completion. A conclusive lookup failure on a complete class owner now reports
`UndeclaredQualifiedIdentifier` (or `AmbiguousQualifiedLookup` for an ambiguous
member) through the normal diagnostic path instead of being replayed into a
generic non-constant initializer diagnostic. A member template is resolved by its
full qualified name first; a template that merely shares the terminal simple name
keeps the result deferred instead of marking it bound.

Replay remains for initializers whose dependent unqualified call still needs the
definition-context lookup state established by source replay, and for qualified
owners that are still incomplete while their specialization is being evaluated.
Qualified result classification performs ordinary member lookup (including base
classes) rather than inferring the outcome from class layout completeness alone.
A rebound dependent record is retired only after the terminal member lookup is
conclusive and its namespace is directly resolvable; otherwise the record stays
for a later substitution pass.

A dependent unqualified call that is still not analyzed after post-parse
normalization trips `Normalized dependent unqualified direct-call query remained
NotYetAnalyzed` in `ConstExprEvaluator_Core.cpp`. The reduction is an ADL-only
lookup such as `probe(T{})` with `probe` declared after the template; the
ordinary-lookup resolution path does not currently complete that query.

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
backend cannot compute with. This also blocks reading or writing a `long double`
value through a data-member pointer, although the member-pointer offset itself
round-trips and compares normally.

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

## Namespace-scope pointer-to-array initializers need a materialized row

A pointer-to-array global initialized from a row of an array with no initializer
(`static int cells[512][3]; static int (*row)[3] = &cells[400];`) is rejected as
a non-constant initializer, while the `= {}` form initializes the same row
correctly. An array of such pointers (`static int (*rows[2])[3] = {&cells[0],
&cells[1]};`) also records no data relocations, so both elements stay null. The
canonical pointee stride applies once the initializer evaluates; what remains is
constant-evaluating `&array[index]` for an unmaterialized static array and
collecting one relocation per pointer leaf of an array whose element is a
pointer-to-array. Record-row pointers at namespace scope reach the same
non-constant path.

## Windows FH3 unwinding still skips function-scope destructors in some try layouts

Windows cleaner emission registers function-scope destructors as FH3 unwind-map
actions. When the function also contains a try block, the cleanup is chained to a
top-level try state only when that try has no cleanup landing of its own. A
top-level try with try-scope destructible locals still leaves the function-scope
cleanup unreachable, and a throw at function scope outside every try region maps
to state -1 in the IP-to-state map. Both cases can skip a function-scope
destructor on Windows even though the ELF path runs it from
`FunctionCleanupLP`. Closing the remaining gap needs explicit EH-state
transitions around construction and teardown, or a writer-level function-cleanup
tail that a synthesized landing state can chain to, rather than the current
IP-range map alone.
