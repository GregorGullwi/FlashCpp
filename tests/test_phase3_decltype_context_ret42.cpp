// Phase 3 test: Decltype context with template disambiguation
// Tests that < after qualified-id in decltype is correctly parsed as template arguments

namespace ns {
    template<typename T>
    int func(T value) {
        return 42;
    }
}

// Test decltype with template arguments in namespace-qualified function call
int test_decltype_with_templates() {
    // This tests the Phase 3 enhancement: ExpressionContext::Decltype
    // The < after ns::func should be recognized as template arguments, not less-than
    using result_type = decltype(ns::func<int>(0));
    
    // Verify the type by using it
    result_type value = ns::func<int>(10);
    
    return value;
}

int main() {
    return test_decltype_with_templates();
}
