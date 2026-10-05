# Known Issues

## WSL front end crashes while processing the libstdc++ `<typeinfo>` test

Compiling `tests/std/test_std_typeinfo_ret0.cpp` with the WSL Debug compiler
currently ends in a SIGSEGV before code generation. The captured stack runs
through `AstToIr::tryEvaluateAsConstExpr<MemberAccessNode>`, constexpr
destructor evaluation, and `StructTypeInfo::buildRTTI()`. This is a front-end
failure while processing the standard header, separate from Itanium symbol
mangling; the typeinfo test therefore does not currently verify the mangling
change on WSL.

## Complex member-type class-template bases still need stack coverage

The base-instantiation worklist now expands concrete pack-expanded bases and
schedules single-segment, non-template member aliases whose targets are
class-template specializations. The 1,024-level regressions
`tests/test_deep_pack_expanded_base_chain_ret0.cpp` and
`tests/test_deep_member_type_base_chain_ret0.cpp` cover those paths; the pack
and alias regressions also pass at their existing shallow depths. These checks
use the shipping Windows compiler build settings, with no stack-reserve change.

More complex member-type paths still fall back to ordinary instantiation:
multiple member segments, member-template segments, and member aliases whose
targets are not directly materialized class-template specializations. Stack
usage for deep chains through those forms remains unverified. Extend dependency
scheduling to cover them while sharing base substitution semantics with
ordinary instantiation.

## Member class template dependent bases are not instantiated

A member class template whose base is one of its own type parameters does not
inherit the base when the member template is instantiated:

```cpp
struct Payload { int v; };
template <class T> struct A { template <class U> struct B : U { }; };
A<int>::B<Payload> b;   // error: member 'v' not found in struct 'B$<hash>'
```

A concrete base works, and a dependent base in a nested class of a class
template works, so the gap is the member template's own parameter not being
substituted into its base list. A class nested inside a member class template
with a dependent base (`struct Inner : U { };`) reaches the same path. This is
independent of nested-class parsing, which now works.

## Replayed function-template local classes can reach codegen without coherent TypeInfo ownership

A namespace/global function-template body that declares a local class and is
then instantiated can reach IR collection with the internal error
`Sema-ready class entity has inconsistent TypeInfo ownership`. The same replay
path works for local typedefs. Direct local classes in non-template functions
now have lexical-scope-owned EntityIds and distinct TypeInfo lookup keys, but
that owner does not identify a local class across function-template
specializations. The remaining replay defect needs an owner that includes the
concrete specialization identity; a transient replay ScopeId or type spelling
is not sufficient.

## Boundary 2F removed unsupported legacy negative fixtures

On 2026-08-30 boundary 2F removed the frozen `_fail.cpp` inventories and their
temporary internal-failure compatibility. The remaining fixtures were not
converted into diagnostic contracts because their failures are outside an
existing bounded owner. Their source is preserved as non-discovered `.cpp.txt`
reproducers under `tests/unsupported_boundary_2f/`:

- Template deduction or constraint failures that collapse to the generic
  template-instantiation rejection: `concept_error_test_fail.cpp`,
  `template_call_wrong_placeholder_base_fail.cpp`,
  `template_concrete_undeduced_fail.cpp`,
  `test_const_rvalue_reference_before_pack_lvalue_fail.cpp`,
  `test_constrained_auto_double_fail.cpp`,
  `test_function_template_recursive_trailing_return_fail.cpp`,
  `test_template_callable_operator_const_receiver_explicit_member_fail.cpp`,
  and `test_template_member_call_const_receiver_fail.cpp`.
- Declaration-parser failures masked by the top-level expression fallback:
  `test_mismatch_args_fail.cpp`, `test_mismatch_const_fail.cpp`,
  `test_mismatch_return_fail.cpp`, `test_pointer_const_mismatch_fail.cpp`,
  `test_reference_const_mismatch_fail.cpp`, and
  `test_template_member_func_template_const_ref_return_fail.cpp`.
- Out-of-line and injected-identity fixtures that require identity recovery:
  `test_injected_identity_ool_namespace_owner_fail.cpp`,
  `test_template_nested_ool_ctor_template_alias_target_mismatch_fail.cpp`,
  `test_template_ool_member_template_single_candidate_alias_target_mismatch_fail.cpp`,
  `test_template_ool_plain_member_multi_param_late_mismatch_fail.cpp`,
  `test_template_ool_plain_member_single_candidate_alias_target_mismatch_fail.cpp`,
  `test_template_partial_spec_ool_ctor_template_alias_target_mismatch_fail.cpp`,
  and `test_template_partial_spec_ool_plain_member_alias_target_mismatch_fail.cpp`.
- Three tests that previously depended on status-2 compatibility:
  `test_operator_subscript_const_ambiguity_fail.cpp`,
  `test_template_lazy_static_member_implicit_this_fail.cpp`, and
  `test_template_out_of_line_static_member_implicit_this_fail.cpp`.

These archived reproducers should return to executable discovery only after
their owning parser, identity, constexpr, or lowering work can provide a stable
diagnostic contract without adding recovery solely to empty the old inventory.
The per-file recovery map is indexed in
`tests/unsupported_boundary_2f/README.md`.

## Legacy flat consumers cannot yet handle interleaved pointer/array declarators

Boundary 3A is not complete: descriptors carry authoritative
`structural_type_id` identity where canonical import is supported and retain
flat fields as compatibility projections, but unmigrated consumers still read
those fields directly. Template argument and substitution storage and general
IR layout/subscript consumers still read parallel flat pointer/array fields, so
they remain vulnerable to projection drift.
Ordered declarators over alias array, reference, function, or member-pointer
wrappers remain unsupported. A three-hop alias-template forwarding chain can
lose the pointee array extent even when one forwarding alias preserves it:
`A0<T, N> = T (*)[N]`, `A1<T, N> = A0<T, N>`, `A2<T, N> = A1<T, N>`, and
`A3<T, N> = A2<T, N>` make `sizeof(*p)` fail to retain the full array size
for `A3<T, 3> p`; a 64-hop probe reported `sizeof(T)` instead.
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

The general flat conversion-descriptor path also rejects ordered declarators
over aliases with callable, array, reference, or member-pointer wrappers. Static
member semantic identity must remain canonical and must not be flattened to
bypass this consumer migration gap.

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
dispatch).

The following remain unsupported and are rejected or left unresolved:

- Taking the address of a **virtual** member function throws
  `CompileError`; the 64-bit representation cannot carry the vtable-index
  encoding the ABI requires.
- Multiple-inheritance adjustment of a member-function pointer is not applied.
- Instantiating a member template address whose signature contains a class
  template in both the return and parameter (or that shares a name with a
  non-template overload) can still route instantiation through a parser-owned
  root. `ensureMemberFunctionAddressMaterialized` logs a warning and leaves the
  address unresolved in that case rather than failing the translation unit, so
  the old uninitialized-slot behavior can still appear there.
- Calling through a materialized member-function pointer is a separate,
  still-unimplemented path (it emits an ``.()`` symbol).

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

## Unity arithmetic test can overflow the native stack

The unity test executable crashes with `SIGSEGV - Stack overflow` in `Arithmetic
operations and nested function calls`
(`tests/FlashCppTest/FlashCppTest/FlashCppTest/FlashCppTest.cpp:1344`) when
linked with the default 1 MiB stack; it was reproduced on 2026-09-07:

```text
C:\Program Files\LLVM\bin\clang-cl.exe /nologo /std:c++20 /EHsc /W3 /I src /I tests\external\doctest /I external /I tests\FlashCppTest\FlashCppTest\FlashCppTest tests\FlashCppTest\FlashCppTest\FlashCppTest\FlashCppTest.cpp /Fe:x64\enum-publication-unit\FlashCppTest.exe
x64\enum-publication-unit\FlashCppTest.exe --test-case="Arithmetic operations and nested function calls"
```

The selected test alone crashes before assertions; exact enum-publication tests
pass. This is unrelated to canonical enum publication. Owner: arithmetic
expression / nested-call test path. The unit-test harness now links the same
32 MiB stack reserve the shipping Windows binary uses
(`FlashCppTest.vcxproj` `StackReserveSize`, and `/link /STACK:33554432` for
direct-driver builds), so both harness paths pass on 2026-09-14. This is a
test-harness parity fix, not a compiler fix: the production compiler's own
stack limit stays what the shipping build already linked, and the underlying
native-stack pressure is masked, not fixed — it must not be used to declare the
path safe or to raise the shipping compiler's stack limit.

This aligns with the authoritative rearchitecture plan's stack and recursion
policy: source-controlled parser, expression, substitution, template, and
semantic work must not grow native stack with logical depth; the target is at
least 1,024 template-instantiation levels under the normal OS stack limit with
nearly constant native-stack use. Defer the underlying investigation and
bounded-depth regression to architecture boundary 10E (bounded parser control
flow), coordinating with the template/semantic worklist migration where the
measured path crosses that boundary. Do not pursue a standalone stack-reserve
change before that work.

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

## Mixed aggregate return through a by-value function template corrupts a field

A small reproducer, well below 256 temporaries, fails on the unchanged baseline
as well as the current compiler:

```cpp
struct Payload { long long wide; double fraction; int small; };
template<class T> T identity(T value) { return value; }
int exercise(int input) {
    Payload result = identity(Payload{0x123456789LL + input, 2.5, input});
    long long wide = identity(result.wide) + 7;
    return wide == 0x123456789LL + input + 7 ? 0 : 3;
}
int main() { return exercise(2); }
```

The wide-field check returns 3. Direct aggregate initialization passes. This
requires a separate investigation of by-value aggregate argument/return storage;
it is independent of numeric temporary identity.

## Temporary frame pre-counting still omits some producers and padding

The temporary-size pre-scan does not publish every typed producer (including
conversion, string-literal, heap-allocation, and function-address operations).
Those slots are allocated during emission. The initial temporary cursor also
includes padding not represented in the pre-count. These limitations predate
numeric temporary storage. Extending the frame's lowest occupied offset during
emission does not consistently reserve outgoing argument/home space below newly
allocated slots. The numeric table removes identity collisions but does not by
itself complete frame layout for these producers. Consolidate producer storage
publication and keep outgoing storage below every allocated local slot; add a
regression that makes a callee write its home/stack-argument area.

The Windows throw-slot path also advances the temporary cursor without the checked
arithmetic used by numeric temporary allocation. Lambda `__invoke` generation
resets temporary numbers without clearing global reference metadata. Both require
separate boundary regressions and investigation; no new name-based type recovery
should be introduced to compensate for either path.
