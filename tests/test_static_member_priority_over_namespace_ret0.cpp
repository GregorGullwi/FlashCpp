// Test that static member function calls have priority over namespace
// template functions (like std::min) when inside a class body
namespace std {
    template<typename T>
    const T& min(const T& a, const T& b) {
        return b < a ? b : a;
    }

    template<typename T>
    struct numeric_limits;

    struct my_type { int val; };

    template<>
    struct numeric_limits<my_type> {
        static constexpr my_type min() noexcept { return my_type{0}; }
        static constexpr my_type max() noexcept { return my_type{100}; }
        // This calls min() which should resolve to the static member, not std::min
        static constexpr my_type lowest() noexcept { return min(); }
    };
}

int main() {
    std::my_type result = std::numeric_limits<std::my_type>::lowest();
    return result.val;  // Should be 0
}
