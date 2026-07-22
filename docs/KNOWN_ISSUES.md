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

## Function types lack one canonical semantic owner
Complete function-type metadata is currently copied among `TypeSpecifierNode`,
alias `TypeInfo`, `TemplateTypeArg`, and `FunctionSignature`.

The first canonical-owner slice removed `StructMember` as a signature source,
deleted the associated member-name scans, made substituted alias metadata take
precedence, and requires every concrete function-pointer type reaching member
registration to already have a complete signature. `StructMember` remains a
consumer copy used by later lowering, but it can no longer repair an incomplete
template producer.

The second slice removed the remaining registration-time original-type and
template-argument search. Template substitution now resolves an alias to its
terminal template-parameter identity, applies the bound `TemplateTypeArg`, and
attaches the substituted `FunctionSignature` to the produced `TypeSpecifierNode`.
Registration consumes only that node and treats a concrete callable without a
signature as an internal producer error. Reading the original pattern and its
bound argument during substitution is part of the semantic substitution step;
doing the same lookup later from a member consumer would be recovery.

The current representation can fail for alias-category function pointers, packs,
transformed template parameters, dependent member-function-pointer owners,
dependent `noexcept` expressions, and nested callable parameter/return types.
Missing metadata is often detected only during indirect-call code generation;
stale metadata can instead affect specialization identity, mangling, or ABI
lowering without an immediate error.

The remaining intended invariant is that substitution produces one canonical
callable `TypeSpecifierNode`/`TypeInfo` containing the complete substituted
function type.
Member registration, mangling, overload resolution, and code generation should
consume that object directly. Remaining work should reduce the parallel metadata
copies and extend structured substitution to the unsupported function-type
components above; a concrete callable type without complete metadata is already
treated as an internal producer error.

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
