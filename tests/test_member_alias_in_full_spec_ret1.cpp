// Test: Member template alias inside full specialization
// This pattern appears in std::__conditional in <type_traits>

template<bool>
struct __conditional {
    template<typename _Tp, typename>
    using type = _Tp;
};

template<>
struct __conditional<false> {
    template<typename, typename _Up>  // This line should parse
    using type = _Up;
};

int main() {
    // Test that we can use it
    __conditional<true>::type<int, double> x = 5;
    __conditional<false>::type<int, double> y = 3.14;
    
    return (x == 5 && y == 3) ? 0 : 1;
}
