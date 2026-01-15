// Test case for static constexpr member visibility in template partial specializations
// Tests fix for GitHub issue: local static constexpr variables like __g and __d2 need to be
// visible when used as template arguments within the same struct body
// This pattern is used extensively in <ratio> header

template<unsigned long long N>
struct holder {
    static constexpr unsigned long long value = N;
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
    static constexpr unsigned long long __d2 = 10 / __g;  // Uses __g
    
    // typedef uses __d2 as a template argument - THIS WAS PREVIOUSLY FAILING
    typedef holder<__d2> __d_type;
    
    // typedef uses __g as a template argument - THIS WAS ALSO FAILING
    typedef holder<__g> __g_type;
    
public:
    typedef __d_type type;
};

int main() {
    return 0;
}
