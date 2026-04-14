// Phase 4 test: SFINAE viability check via explicit placeholder state
// Tests that enable_if<false>::type causes SFINAE elimination
// (dependent member-alias placeholder detected via DependentPlaceholderKind, not string heuristic)
template<bool B, typename T = void>
struct enable_if {};

template<typename T>
struct enable_if<true, T> {
    using type = T;
};

template<typename T>
struct is_integral {
    static constexpr bool value = false;
};

template<>
struct is_integral<int> {
    static constexpr bool value = true;
};

template<>
struct is_integral<long> {
    static constexpr bool value = true;
};

// SFINAE: only viable when T is integral
template<typename T>
typename enable_if<is_integral<T>::value, int>::type check(T) { return 1; }

// Non-template fallback
int check(double) { return 0; }

int main() {
    int a = check(42);       // should select template overload (is_integral<int>::value == true)
    int b = check(3.14);     // should select non-template fallback
    return a - 1 + b;        // 1 - 1 + 0 = 0
}
