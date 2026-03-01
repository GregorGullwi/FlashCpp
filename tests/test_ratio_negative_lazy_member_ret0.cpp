// Test: Negative values in lazy static member constant folding
// Validates that ratio<1, -2> produces correct negative num and positive den
// via __static_sign pattern: integral_constant<int, (_Pn < 0) ? -1 : 1>
// Bug: uint64_t cast of negative int64_t produced huge positive numbers

template<typename T, T v>
struct integral_constant {
    static constexpr T value = v;
};

template<int _Pn>
struct static_sign : integral_constant<int, (_Pn < 0) ? -1 : 1> { };

template<int _Pn>
struct static_abs : integral_constant<int, _Pn * static_sign<_Pn>::value> { };

template<int _Num, int _Den = 1>
struct ratio {
    static constexpr int num = _Num * static_sign<_Den>::value;
    static constexpr int den = _Den * static_sign<_Den>::value;
};

int main() {
    // Negative denominator: ratio<1, -2> -> num=-1, den=2
    static_assert(ratio<1, -2>::num == -1);
    static_assert(ratio<1, -2>::den == 2);
    
    // Negative numerator: ratio<-3, 2> -> num=-3, den=2
    static_assert(ratio<-3, 2>::num == -3);
    static_assert(ratio<-3, 2>::den == 2);
    
    // Both negative: ratio<-3, -2> -> num=3, den=2
    static_assert(ratio<-3, -2>::num == 3);
    static_assert(ratio<-3, -2>::den == 2);
    
    // Return value depends on negative value being handled correctly
    return ratio<1, -2>::num + 1; // -1 + 1 = 0
}
