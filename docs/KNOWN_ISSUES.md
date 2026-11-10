# Known Issues

This file is the authoritative inventory of unresolved gaps. Migration status
and validation belong in `MIGRATION_PROGRESS.md`; that file should link here
instead of restating issue details.

## Operator candidate lookup still lacks complete member-name provenance

Direct unary, binary, and assignment operator lookup now stop at the first class
that declares the operator name, while using-declarations restore only nominated
base overloads. Requires-expression assignment viability consumes the resolved
no-match or ambiguous operator state instead of accepting the expression from its
left-hand type alone. Remaining work is persistent using-declaration provenance
through template instantiation and replay. The parser still retains only imported
spelling in temporary class parsing state for some instantiated forms, so
member-name lookup sets with hiding and base ambiguity are not yet shared across
every canonical and compatibility consumer. Direct expressions over an already
instantiated class can use `using Base<T>::operator-` correctly, but
requires-expression validation may run before that instantiation/rebinding and
therefore reports the requirement false. Forcing that ordering overlaps
Boundary 8A class-template instantiation.

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

Nested anonymous-union-in-anonymous-union hierarchy still needs an explicit
representation; flat contiguous layout groups alone cannot distinguish an
inactive outer union from an active outer variant containing an inactive inner
union.

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

Non-null object-, function-, and member-pointer template arguments now carry
published target identity; function pointers also carry their target signature
and member pointers their canonical member value. Canonical import fails closed
for object-pointer element offsets outside the canonical 32-bit range or targets
without a published identity.

## Expression parsing and constraint evaluation can overflow a 1 MB native stack

The nested NTTP-field regression
`test_nested_inner_value_pack_member_field_ret0.cpp` overflows during compilation
with a 1 MB reserve. The nested type-pack field regression fits at 1 MB; that
result does not establish stack safety for the NTTP path. Surrounding template
materialization still needs smaller frames and bounded scheduling.
Parenthesized `requires` operands in concept definitions can also overflow a
1 MB stack in expression parsing before constraint evaluation starts, for example
`Base<T> && (requires(T value) { value.probe(); } || __is_integral(T))`.
Named-concept evaluation also retains native recursion: a 16-level chain of
`Next<T> = Previous<T> && Base<T>` also crashes with a 1 MB compiler reserve,
even with a builtin-trait leaf. Constraint traversal needs bounded scheduling as well.

Persistent payload handles and out-of-line argument vectors are not reclaimed
when rolled-back probes abandon entries; they remain allocated until process
exit. Spans into these vectors remain valid only until their next mutation.

## Pack substitution rollback and diagnostics remain incomplete

Runtime function-parameter fold replay still uses its separate legacy adapter.
The legacy structural matcher still has its 64-level nested-argument guard.
Alias and variable-template substitution still have flattened input adapters
that need migration to grouped bindings; nested pack NTTP aliases remain
unsupported through those adapters. Member class-templates now parse
pack-expanded bases such as `List : U...`; remaining base-template-id gaps are
tracked separately.

C++20 permits observable class-instantiation effects before a later deduction
failure and requires errors in separately instantiated bodies to remain hard
errors. These required effects must not be rolled back with candidate-local
state.

Complete signature substitution can still retain earlier component identities
after a later failure, and nested callable signature objects are shared by the
legacy representation. Establish owned candidate signature storage before
extending rollback across that walk.

`FrontendScratchTransaction` now also journals legacy type-registry publication:
rollback restores name/native bindings and truncates newly published top-level
`TypeInfo` rows plus their index-keyed cold payloads (dependent-name records,
template-argument lists and intern buckets, template-argument cold payloads, and
instantiation contexts). The pointer-keyed legacy `StructTypeInfo`/`EnumTypeInfo`
arenas also truncate when the discarded range is unreachable; a range still
referenced by a surviving row is retained instead of left dangling. Retention
scans surviving `TypeInfo` cold indices, interned argument lists, instantiation
contexts, and dependent-name records without allocating during rollback. Owners
outside those stores, such as a `QualifiedIdentifierNode` or a copied
`TemplateArgInfo` held by the AST/template registries, are not yet enumerable, so
in-place rewrites of those external owners remain outside the boundary. Legacy
AST nodes, parser publication, and direct `TypeInfo` field writes that bypass
the mutable accessor and mutator API still need an owned transaction boundary,
and production speculative parsing is not yet routed through it. Do not wrap the whole materialization path in the transaction or
erase required dependency instantiations. Lockstep member and namespace-scope
expansions now report
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

## Decltype operator overload resolution is parser-owned

`get_overload_return_type` in `Parser_Expr_QualLookup.cpp` re-resolves a binary
operator overload via `annotateConcreteBinaryOperatorOverload` when it is not
already resolved, guarded by `resolving_overload_in_get_expression_type_`. This
keeps `sizeof(decltype(expr))` working for inherited operators on specialized
class templates, but it is a parser-level workaround. The proper owner is
semantics (boundary 4): sema should own operator-overload resolution and result
types, and the parser should only consume the resolved type. Re-evaluate and
remove this re-resolution when boundary 4 starts.

## Binary operator compatibility fallback removal is blocked

Removing `gatherCompatibilityMembers()` from the `!left_type_id` path in
`findBinaryOperatorOverloadWithFreeFunction` (OverloadResolution.h) breaks
`test_inherited_operator_published_graph_ret0.cpp`: the canonical path finds
the inherited operator but `sizeof(decltype(specialized + 5))` does not equal
`sizeof(long long)`. The alias-resolution fix for `tryGetCanonicalOverloadClassTypeId`
and the inherited-result-type fix in `get_overload_return_type` are in place,
but the canonical binary path still produces the wrong result type for inherited
operators on specialized aliases. The fallback stays until the result-type
derivation is fixed. This is a boundary 3A item-1 sub-task.

The triviality and lifetime traits still read `StructTypeInfo` and need a
published member-property schema. A lazy-constraint member-object-pointer trait
case remains unresolved because its member-owner identity is not published at
materialization. The sema-owned construction traits
(`__is_constructible`, `__is_trivially_constructible`, and
`__is_nothrow_constructible`) answer exact matches, supported scalar and
reference conversions, same-class by-value arguments, publicly derived by-value
arguments whose base publishes a usable public copy or move constructor,
unrelated class arguments with exactly one viable non-explicit public converting
constructor, and omitted default arguments from published constructor schemas.
They still defer for conversion-operator targets, multiple or undecidable
converting candidates, derived bases whose copy/move facts are unpublished or
inaccessible, incomplete schemas, and unresolved canonical types. Explicit and
deleted converting constructors stay fail-closed. Inaccessible converting
constructors are not accepted canonically but still defer to the compatibility
resolver, which currently reports them as constructible.
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

## `override` validation does not traverse virtual base classes

A member function marked `override` is rejected when the overridden virtual
function is reached through a base class that itself has a virtual base:

```cpp
struct Base { virtual int value() const { return 1; } };
struct Mid : virtual Base { int value() const override { return 2; } };  // accepted
struct Leaf : Mid { int value() const override { return 3; } };          // rejected
```

`Leaf::value` overrides `Mid::value` (and `Base::value`); Clang C++20 accepts
the program and virtual dispatch through a `Base*` returns 3. FlashCpp reports
`function 'value' marked 'override' but does not override any base class function
in class 'Leaf'`. Removing `override` makes the same program compile and dispatch
correctly, so the defect is confined to override validation's base-function set.
The fix is to collect virtual-base members (or their final overriders) before
deciding that no function is overridden. Discovered 2026-11-09 while verifying
the "virtual functions: full support" claim.
