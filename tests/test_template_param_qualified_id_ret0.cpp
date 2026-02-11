// Test: Template parameter used in expression substitution via QualifiedIdentifier
// Tests _R1::member pattern where _R1 is a template type parameter

template<int N, int D>
struct Ratio {
    static constexpr int num = N;
    static constexpr int den = D;
};

template<typename R1, typename R2>
struct RatioAdd {
    static constexpr int result = R1::num + R2::num;
};

int main() {
    using R1 = Ratio<3, 1>;
    using R2 = Ratio<4, 1>;
    return RatioAdd<R1, R2>::result - 7;  // Should return 0
}
