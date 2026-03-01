// Test: static constexpr member with template-dependent qualified identifiers
// like _R1::num needs lazy instantiation to properly substitute template params.

namespace std {

template<long long _Num, long long _Den = 1>
struct ratio {
    static constexpr long long num = _Num;
    static constexpr long long den = _Den;
};

template<typename _R1, typename _R2>
struct ratio_equal {
    static constexpr bool value = _R1::num == _R2::num && _R1::den == _R2::den;
};

} // namespace std

int main() {
    static_assert(std::ratio_equal<std::ratio<1, 2>, std::ratio<1, 2>>::value == true);
    static_assert(std::ratio_equal<std::ratio<1, 2>, std::ratio<2, 4>>::value == false);
    static_assert(std::ratio_equal<std::ratio<3, 5>, std::ratio<3, 5>>::value == true);
    return 0;
}
