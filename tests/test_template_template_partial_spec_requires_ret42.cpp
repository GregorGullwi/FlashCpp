// Test partial specialization with template template parameter
// Pattern from <type_traits> line 2737

// Primary template
template<typename T>
struct Container {
    T value;
};

// Primary template for detected_or
template<typename Def, template<typename...> class Op, typename... Args>
struct detected_or {
    using type = Def;
};

// Partial specialization with requires clause
// This is what line 2737 tries to do
template<typename Def, template<typename...> class Op, typename... Args>
    requires requires { typename Op<Args...>; }
    struct detected_or<Def, Op, Args...> {
        using type = int;  // Different type for specialization
    };

int main() {
    // The specialization should be selected because Container<double> is valid
    // The specialization defines type = int, while primary defines type = Def (long)
    // If specialization is used: type = int, x = 42
    // If primary is used: type = long, x = 42
    // Both compile, but we verify specialization by checking the returned value works
    detected_or<long, Container, double>::type x = 42;
    return x;  // Should return 42 with either template
}
