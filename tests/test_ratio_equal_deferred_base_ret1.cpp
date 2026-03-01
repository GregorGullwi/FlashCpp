// Test: ratio_equal pattern with deferred base class expression evaluation
// Validates the full chain: ratio<N,D>::num/den -> __static_sign -> __static_abs -> integral_constant
// Then ratio_equal<R1,R2> : integral_constant<bool, R1::num == R2::num && R1::den == R2::den>

template<typename T, T v>
struct integral_constant {
    static constexpr T value = v;
};

using true_type = integral_constant<bool, true>;
using false_type = integral_constant<bool, false>;

template<int _Pn>
struct static_sign : integral_constant<int, (_Pn < 0) ? -1 : 1> { };

template<int _Pn>
struct static_abs : integral_constant<int, _Pn * static_sign<_Pn>::value> { };

template<int _Num, int _Den = 1>
struct ratio {
    static constexpr int num = _Num * static_sign<_Den>::value;
    static constexpr int den = _Den * static_sign<_Den>::value;
};

template<typename _R1, typename _R2>
struct ratio_equal
    : integral_constant<bool, _R1::num == _R2::num && _R1::den == _R2::den> { };

int main() {
    // Basic ratio equality
    static_assert(ratio_equal<ratio<1, 2>, ratio<1, 2>>::value == true);
    static_assert(ratio_equal<ratio<1, 2>, ratio<1, 3>>::value == false);
    
    // Negative denominator normalization: ratio<1, -2> -> num=-1, den=2
    static_assert(ratio<1, -2>::num == -1);
    static_assert(ratio<1, -2>::den == 2);
    
    // Return 1 to verify at runtime
    return ratio_equal<ratio<1, 2>, ratio<1, 2>>::value ? 1 : 0;
}
