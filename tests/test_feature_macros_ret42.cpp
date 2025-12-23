// Test that library feature test macros are defined
int main() {
    int result = 0;
    
    // Check __cpp_lib_type_trait_variable_templates
    #ifdef __cpp_lib_type_trait_variable_templates
    result += 1;
    #endif
    
    // Check __cpp_lib_addressof_constexpr
    #ifdef __cpp_lib_addressof_constexpr
    result += 1;
    #endif
    
    // Check __cpp_lib_integral_constant_callable
    #ifdef __cpp_lib_integral_constant_callable
    result += 1;
    #endif
    
    // Check __cpp_lib_is_aggregate
    #ifdef __cpp_lib_is_aggregate
    result += 1;
    #endif
    
    // Check __cpp_lib_void_t
    #ifdef __cpp_lib_void_t
    result += 1;
    #endif
    
    // Check __cpp_lib_bool_constant
    #ifdef __cpp_lib_bool_constant
    result += 1;
    #endif
    
    // Should have 6 features defined = 42
    return result * 7;  // 6 * 7 = 42
}
