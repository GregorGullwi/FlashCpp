# Known Issues

## Nested class-template members lack a canonical current-instantiation owner during constructor substitution
When a nested class is instantiated as part of its enclosing class template,
its constructors are copied before the nested `TypeInfo` is registered. A
user-written copy constructor such as `Inner(const Inner&)` therefore cannot
rewrite the pattern's injected-class-name type to the concrete nested owner.
Copy initialization inside a nested member can then reject the valid copy
constructor and diagnose an unrelated `explicit` converting constructor.

The sustainable fix is to register the nested semantic type and owner identity
before copying constructor signatures, then use that typed identity in the
shared current-instantiation rewrite. Do not repair this with nested-name scans
or overload-resolution guesses.

## Modular-build-only crash: test_template_partial_spec_ool_ctor_template_same_name_overload_ret0.cpp
`ConstructorDeclarationNode::has_template_parameters()` dereferences a null inner
pointer in `InlineVector::size()` during `materializeMatchingConstructorTemplate`.
Only manifests in the Modular build (runs fine under Sharded/Unity). Root cause
appears to be a default-constructed `ConstructorDeclarationNode` whose `InlineVector`
data pointer is never initialized — the Sharded build's different TU arrangement
happens to zero the memory. See `Parser_Templates_Inst_MemberFunc.cpp:1394`.

## Missing ambiguity diagnostics in overload resolution
Some overload sets that are ill-formed in standard C++20 due to ambiguity are not
consistently diagnosed as compile errors yet.

- `tests/test_operator_member_tiebreak_ret0.cpp` (original form): both member and
  non-member `operator+` are viable for `lhs + rhs`; this should be ambiguous.
- `tests/test_variadic_overload.cpp` (original form): `log(2, "Test")` is
  ambiguous between `void log(int, const char*)` and
  `void log(int, const char*, ...)`.

## Operator overload resolution mismatch (`operator&`)
The original `tests/test_operator_addressof_overload_baseline_ret99.cpp` behavior
documented a conformance gap where unary `&` on class type did not reliably
select overloaded `operator&` as required, effectively collapsing behavior toward
`__builtin_addressof`.

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
