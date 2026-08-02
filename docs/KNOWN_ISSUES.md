# Known Issues

## Virtual-base derived-to-base conversions (implemented)

Virtual inheritance itself is implemented and covered by the existing positive
tests, including `tests/test_virtual_base_classes_ret160.cpp`, which constructs
a virtual diamond and verifies the single shared base subobject. RTTI and
exception matching through virtual bases are covered separately by the
`test_eh_*virtual_base*` tests. These tests exercise virtual-base layout,
member access, vtables, RTTI, and exception handling. The standard implicit
derived-to-base conversion forms are now covered by the positive regression
`tests/test_virtual_base_derived_to_base_ret0.cpp`, including:

- reference binding to a virtual base;
- pointer conversion through the complete object and through intermediate
  virtual-base subobjects, including the null-pointer rule;
- copy-initialization of a base object from a derived object;
- passing and returning a base object by value.

Semantic analysis classifies public, inaccessible, and ambiguous inheritance
paths according to C++20 [conv.ptr] and [conv]. The IR carries a dedicated
virtual-base adjustment operation. The backend resolves the actual subobject
address at runtime from the object's virtual-base metadata, while non-virtual
paths continue to use their fixed subobject offset. By-value conversion invokes
the selected base copy/move constructor after forming the adjusted base
subobject address; it does not use a raw byte-copy shortcut.

The runtime metadata is an implementation detail of FlashCpp's object model.
Missing or inconsistent finalized metadata is diagnosed as an internal compiler
error; no conversion is synthesized without the required metadata.

## Deferred `<tuple>` member emission and `swap` gaps

The full `<tuple>` header still fails during deferred member emission after the
reduced alias/materialization cases have completed. Constructor emission and
some `swap` overload probes remain incomplete. The reduced partial-spec nested
typedef shape (`using Ttype = Tuple<This, Rest...>`) is covered by
`tests/test_dependent_alias_tuple_element_get_ret0.cpp` and now materializes.
The regression models an MSVC `tuple_element` / `get` chain with a dependent
`ElemT = typename Elem<I, T>::type` alias and an index-zero partial
specialization whose nested `Ttype = Tuple<This, Rest...>` must expand the
pack and materialize a concrete `Tuple` specialization. It ODR-uses `Ttype`
through a pointer cast so a dependent placeholder cannot survive into IR.
Reference-returning member access through `static_cast<Ttype&>` is covered
separately by `tests/test_static_cast_ref_member_address_ret0.cpp`.

These remaining failures are generic deferred-member and overload-probe gaps,
not library-name problems.

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
it is blocked on broader `long double` codegen support: FlashCpp currently has type
identity, some constexpr/overload/builtin coverage, and inconsistent sizing (80-bit
x87 extended in places, Windows-style 8-byte `double` alias elsewhere), but no real
x87 load/store/`%st0` emission path. Constexpr evaluation often collapses
`long double` to `double`. Do not paper over return ABI with size guesses or INTEGER
fallbacks; wait until `long double` lowering can emit the SysV x87 convention.
