// Test variable template partial specialization with template instantiation pattern
// Pattern: __is_ratio_v<ratio<_Num, _Den>> should match ratio<1,2>
namespace std {
    template<long _Num, long _Den>
    struct ratio { };
    
    // Primary variable template
    template<typename _Tp>
    constexpr bool __is_ratio_v = false;
    
    // Partial specialization for ratio<N,D>
    template<long _Num, long _Den>
    constexpr bool __is_ratio_v<ratio<_Num, _Den>> = true;
}

int main() {
    static_assert(std::__is_ratio_v<std::ratio<1,2>> == true, "ratio should match specialization");
    static_assert(std::__is_ratio_v<int> == false, "int should use primary");
    return 1; // Return 1 to verify the test ran
}
