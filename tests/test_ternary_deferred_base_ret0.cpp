// Test: Ternary operator in deferred base class expression
// Tests the __static_sign pattern from <ratio>: integral_constant<int, (_Pn < 0) ? -1 : 1>
// Validates: ExpressionSubstitutor TernaryOperatorNode support,
//            try_evaluate_constant_expression ternary handler with ctx.parser,
//            correct template arg extraction from TypeInfo stored args

template<typename T, T v>
struct integral_constant {
    static constexpr T value = v;
};

template<int _Pn>
struct static_sign : integral_constant<int, (_Pn < 0) ? -1 : 1> { };

template<int _Pn>
struct static_abs : integral_constant<int, _Pn * static_sign<_Pn>::value> { };

int main() {
    // Test ternary in deferred base
    static_assert(static_sign<5>::value == 1);
    static_assert(static_sign<-3>::value == -1);
    
    // Test abs using sign (chains through __static_sign)
    static_assert(static_abs<5>::value == 5);
    static_assert(static_abs<-3>::value == 3);
    
    return 0;
}
