# Known Issues

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
