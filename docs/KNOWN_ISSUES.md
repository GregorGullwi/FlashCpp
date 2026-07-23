# Known Issues

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

## SysV MEMORY-class aggregate returns need deferred ABI planning

SysV aggregate argument and direct register-return lowering classifies concrete
types from canonical layout metadata. Unaligned aggregates and other MEMORY-class
values are also classified correctly for parameter passing. Return-slot selection,
however, is still decided while IR is generated from a size threshold. Replacing
that threshold directly with strict layout classification exposes function
templates and deferred lambda signatures whose return types are not concrete at
that phase.

The remaining C++20-compliant fix is to represent return ABI disposition as
`direct`, `indirect`, or `dependent`, defer the last case until substitution has
produced a complete canonical return type, and require concrete call/function IR
to carry the resulting ABI plan. Treating an unresolved type as direct, guessing
from its category or size, or reconstructing it during code generation would hide
the producer defect. Until that planning boundary exists, a small unaligned
aggregate return can still use the wrong direct-return convention on SysV.

The first shared-classifier lowering slice is deliberately limited to concrete
two-eightbyte aggregates (9–16 bytes). Single-eightbyte aggregate lowering still
uses the legacy scalar path, which is correct for INTEGER-class values but does
not yet select an SSE register for aggregates containing only `float`/`double`.
Enabling strict classification for that path currently exposes several IR
producers that omit canonical type identity for small class values. Those
producers must be closed before the scalar path is replaced; accepting an absent
`TypeIndex` as INTEGER would only preserve the same non-standard guess.

Aggregate returns containing `long double` also remain on the legacy path. Their
SysV result classification uses the X87/X87UP classes, which the current backend
return-register abstraction cannot represent yet. Aggregate parameters containing
`long double` are still classified as MEMORY as required.
