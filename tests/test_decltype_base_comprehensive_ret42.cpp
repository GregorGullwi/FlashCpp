// Comprehensive test for decltype base class specification
// Tests both working and limited cases

namespace detail {
    // Simple base classes
    struct result_true {
        static constexpr bool value = true;
    };
    
    struct result_false {
        static constexpr bool value = false;
    };
    
    // Non-template function returning a type
    result_true get_result() { return {}; }
}

// ======== WORKING: Non-template struct with decltype base ========
// This works because the decltype expression can be fully evaluated at parse time
struct test_simple
  : decltype(detail::get_result())
{
    int dummy;  // Add a member so struct isn't empty
};

// ======== LIMITATION: Template-dependent decltype base ========
// This is parsed and deferred correctly, but currently the expression
// cannot be evaluated during template instantiation because template
// parameter substitution in decltype expressions is not yet implemented.
// 
// This pattern is used in <type_traits> line 194:
//   template<typename... _Bn>
//   struct __or_
//     : decltype(__detail::__or_fn<_Bn...>(0))
//     { };
//
// To fully support this, we would need to:
// 1. Substitute template parameters in the expression AST
// 2. Re-evaluate the expression with substituted parameters
// 3. Add the resulting base class to the instantiated struct

int main() {
    // Test the working case
    test_simple t1;
    bool success = t1.value;  // Should be true from result_true
    
    return success ? 42 : 0;
}
