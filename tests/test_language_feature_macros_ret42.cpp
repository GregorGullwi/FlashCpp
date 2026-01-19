// Ensure language feature-test macros are defined with correct C++20 values

#if !defined(__cpp_aggregate_bases) || __cpp_aggregate_bases != 201603L
#error "__cpp_aggregate_bases incorrect"
#endif
#if !defined(__cpp_aggregate_nsdmi) || __cpp_aggregate_nsdmi != 201304L
#error "__cpp_aggregate_nsdmi incorrect"
#endif
#if !defined(__cpp_aggregate_paren_init) || __cpp_aggregate_paren_init != 201902L
#error "__cpp_aggregate_paren_init incorrect"
#endif
#if !defined(__cpp_alias_templates) || __cpp_alias_templates != 200704L
#error "__cpp_alias_templates incorrect"
#endif
#if !defined(__cpp_aligned_new) || __cpp_aligned_new != 201606L
#error "__cpp_aligned_new incorrect"
#endif
#if !defined(__cpp_attributes) || __cpp_attributes != 200809L
#error "__cpp_attributes incorrect"
#endif
#if !defined(__cpp_binary_literals) || __cpp_binary_literals != 201304L
#error "__cpp_binary_literals incorrect"
#endif
#if !defined(__cpp_capture_star_this) || __cpp_capture_star_this != 201603L
#error "__cpp_capture_star_this incorrect"
#endif
#if !defined(__cpp_char8_t) || __cpp_char8_t != 201811L
#error "__cpp_char8_t incorrect"
#endif
#if !defined(__cpp_concepts) || __cpp_concepts != 201907L
#error "__cpp_concepts incorrect"
#endif
#if !defined(__cpp_conditional_explicit) || __cpp_conditional_explicit != 201806L
#error "__cpp_conditional_explicit incorrect"
#endif
#if !defined(__cpp_conditional_trivial) || __cpp_conditional_trivial != 202002L
#error "__cpp_conditional_trivial incorrect"
#endif
#if !defined(__cpp_consteval) || __cpp_consteval != 201811L
#error "__cpp_consteval incorrect"
#endif
#if !defined(__cpp_constexpr) || __cpp_constexpr != 202002L
#error "__cpp_constexpr incorrect"
#endif
#if !defined(__cpp_constexpr_dynamic_alloc) || __cpp_constexpr_dynamic_alloc != 201907L
#error "__cpp_constexpr_dynamic_alloc incorrect"
#endif
#if !defined(__cpp_constexpr_in_decltype) || __cpp_constexpr_in_decltype != 201711L
#error "__cpp_constexpr_in_decltype incorrect"
#endif
#if !defined(__cpp_constinit) || __cpp_constinit != 201907L
#error "__cpp_constinit incorrect"
#endif
#if !defined(__cpp_decltype) || __cpp_decltype != 200707L
#error "__cpp_decltype incorrect"
#endif
#if !defined(__cpp_decltype_auto) || __cpp_decltype_auto != 201304L
#error "__cpp_decltype_auto incorrect"
#endif
#if !defined(__cpp_deduction_guides) || __cpp_deduction_guides != 201907L
#error "__cpp_deduction_guides incorrect"
#endif
#if !defined(__cpp_delegating_constructors) || __cpp_delegating_constructors != 200604L
#error "__cpp_delegating_constructors incorrect"
#endif
#if !defined(__cpp_designated_initializers) || __cpp_designated_initializers != 201707L
#error "__cpp_designated_initializers incorrect"
#endif
#if !defined(__cpp_enumerator_attributes) || __cpp_enumerator_attributes != 201411L
#error "__cpp_enumerator_attributes incorrect"
#endif
#if !defined(__cpp_exceptions) || __cpp_exceptions != 199711L
#error "__cpp_exceptions incorrect"
#endif
#if !defined(__cpp_fold_expressions) || __cpp_fold_expressions != 201603L
#error "__cpp_fold_expressions incorrect"
#endif
#if !defined(__cpp_generic_lambdas) || __cpp_generic_lambdas != 201707L
#error "__cpp_generic_lambdas incorrect"
#endif
#if !defined(__cpp_guaranteed_copy_elision) || __cpp_guaranteed_copy_elision != 201606L
#error "__cpp_guaranteed_copy_elision incorrect"
#endif
#if !defined(__cpp_hex_float) || __cpp_hex_float != 201603L
#error "__cpp_hex_float incorrect"
#endif
#if !defined(__cpp_if_constexpr) || __cpp_if_constexpr != 201606L
#error "__cpp_if_constexpr incorrect"
#endif
#if !defined(__cpp_impl_coroutine) || __cpp_impl_coroutine != 201902L
#error "__cpp_impl_coroutine incorrect"
#endif
#if !defined(__cpp_impl_destroying_delete) || __cpp_impl_destroying_delete != 201806L
#error "__cpp_impl_destroying_delete incorrect"
#endif
#if !defined(__cpp_impl_three_way_comparison) || __cpp_impl_three_way_comparison != 201907L
#error "__cpp_impl_three_way_comparison incorrect"
#endif
#if !defined(__cpp_inheriting_constructors) || __cpp_inheriting_constructors != 200802L
#error "__cpp_inheriting_constructors incorrect"
#endif
#if !defined(__cpp_init_captures) || __cpp_init_captures != 201803L
#error "__cpp_init_captures incorrect"
#endif
#if !defined(__cpp_initializer_lists) || __cpp_initializer_lists != 200806L
#error "__cpp_initializer_lists incorrect"
#endif
#if !defined(__cpp_inline_variables) || __cpp_inline_variables != 201606L
#error "__cpp_inline_variables incorrect"
#endif
#if !defined(__cpp_lambdas) || __cpp_lambdas != 200907L
#error "__cpp_lambdas incorrect"
#endif
#if !defined(__cpp_modules) || __cpp_modules != 201907L
#error "__cpp_modules incorrect"
#endif
#if !defined(__cpp_namespace_attributes) || __cpp_namespace_attributes != 201411L
#error "__cpp_namespace_attributes incorrect"
#endif
#if !defined(__cpp_noexcept_function_type) || __cpp_noexcept_function_type != 201510L
#error "__cpp_noexcept_function_type incorrect"
#endif
#if !defined(__cpp_nontype_template_args) || __cpp_nontype_template_args != 201911L
#error "__cpp_nontype_template_args incorrect"
#endif
#if !defined(__cpp_nontype_template_parameter_auto) || __cpp_nontype_template_parameter_auto != 201606L
#error "__cpp_nontype_template_parameter_auto incorrect"
#endif
#if !defined(__cpp_nullptr) || __cpp_nullptr != 200704L
#error "__cpp_nullptr incorrect"
#endif
#if !defined(__cpp_nsdmi) || __cpp_nsdmi != 200809L
#error "__cpp_nsdmi incorrect"
#endif
#if !defined(__cpp_range_based_for) || __cpp_range_based_for != 201603L
#error "__cpp_range_based_for incorrect"
#endif
#if !defined(__cpp_raw_strings) || __cpp_raw_strings != 200710L
#error "__cpp_raw_strings incorrect"
#endif
#if !defined(__cpp_ref_qualifiers) || __cpp_ref_qualifiers != 200710L
#error "__cpp_ref_qualifiers incorrect"
#endif
#if !defined(__cpp_return_type_deduction) || __cpp_return_type_deduction != 201304L
#error "__cpp_return_type_deduction incorrect"
#endif
#if !defined(__cpp_rtti) || __cpp_rtti != 199711L
#error "__cpp_rtti incorrect"
#endif
#if !defined(__cpp_rvalue_references) || __cpp_rvalue_references != 200610L
#error "__cpp_rvalue_references incorrect"
#endif
#if !defined(__cpp_sized_deallocation) || __cpp_sized_deallocation != 201309L
#error "__cpp_sized_deallocation incorrect"
#endif
#if !defined(__cpp_static_assert) || __cpp_static_assert != 201411L
#error "__cpp_static_assert incorrect"
#endif
#if !defined(__cpp_structured_bindings) || __cpp_structured_bindings != 201606L
#error "__cpp_structured_bindings incorrect"
#endif
#if !defined(__cpp_template_template_args) || __cpp_template_template_args != 201611L
#error "__cpp_template_template_args incorrect"
#endif
#if !defined(__cpp_threadsafe_static_init) || __cpp_threadsafe_static_init != 200806L
#error "__cpp_threadsafe_static_init incorrect"
#endif
#if !defined(__cpp_unicode_characters) || __cpp_unicode_characters != 200704L
#error "__cpp_unicode_characters incorrect"
#endif
#if !defined(__cpp_unicode_literals) || __cpp_unicode_literals != 200710L
#error "__cpp_unicode_literals incorrect"
#endif
#ifndef __cpp_user_defined_literals
#error "__cpp_user_defined_literals incorrect"
#else
static_assert(__cpp_user_defined_literals == 200809L, "__cpp_user_defined_literals incorrect");
#endif
#if !defined(__cpp_using_enum) || __cpp_using_enum != 201907L
#error "__cpp_using_enum incorrect"
#endif
#if !defined(__cpp_variable_templates) || __cpp_variable_templates != 201304L
#error "__cpp_variable_templates incorrect"
#endif
#if !defined(__cpp_variadic_templates) || __cpp_variadic_templates != 200704L
#error "__cpp_variadic_templates incorrect"
#endif
#if !defined(__cpp_variadic_using) || __cpp_variadic_using != 201611L
#error "__cpp_variadic_using incorrect"
#endif

int main() {
	return 42;
}
