// Minimal test for pack expansion in parenthesized expression
template<typename T>
T declval();

template<typename... Args>
struct test {
    // Simplest case: parenthesized pack expansion
    template<typename Fp>
    using type1 = decltype((declval<Args>()...));
};

int main() {
    return 0;
}
