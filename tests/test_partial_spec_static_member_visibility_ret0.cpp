// Test case for static constexpr member visibility in template partial specializations
// Tests fix for GitHub issue: local static constexpr variables like __g and __d2 need to be
// visible when used as template arguments within the same struct body
// This pattern is used extensively in <ratio> header

template<unsigned long long N>
struct holder {
    static constexpr unsigned long long value = N;
};

// Simple ratio-like type to use as template arguments
template<long long Num, long long Den = 1>
struct ratio {
    static constexpr long long num = Num;
    static constexpr long long den = Den;
};

// Primary template
template<typename _R1, typename _R2, bool __b1, bool __b2, bool __b3>
struct __ratio_add_impl;

// Partial specialization where static members are used as template arguments
template<typename _R1, typename _R2, bool __b>
struct __ratio_add_impl<_R1, _R2, true, true, __b> {
private:
    // Static members defined
    static constexpr unsigned long long __g = 5;
    static constexpr unsigned long long __d2 = 10 / __g;  // Uses __g, should be 2
    
    // typedef uses __d2 as a template argument - THIS WAS PREVIOUSLY FAILING
    typedef holder<__d2> __d_type;
    
    // typedef uses __g as a template argument - THIS WAS ALSO FAILING
    typedef holder<__g> __g_type;
    
public:
    typedef __d_type type;
    typedef __g_type g_type;
};

int main() {
    // Instantiate the partial specialization with ratio types
    using R1 = ratio<1, 2>;
    using R2 = ratio<3, 4>;
    
    // This triggers instantiation of __ratio_add_impl<R1, R2, true, true, false>
    using impl = __ratio_add_impl<R1, R2, true, true, false>;
    
    // Verify the static members are correctly computed
    // __g = 5, __d2 = 10 / 5 = 2
    // holder<__d2>::value = 2, holder<__g>::value = 5
    
    // Test that type alias works - it should be holder<2>
    unsigned long long d2_value = impl::type::value;  // Should be 2
    unsigned long long g_value = impl::g_type::value; // Should be 5
    
    // Return 0 if __d2 = 2 and __g = 5 (correct), otherwise return error code
    if (d2_value != 2) return 1;
    if (g_value != 5) return 2;
    
    return 0;  // Success
}
