// Test that static member function calls have priority over namespace
// template functions when inside a class body
namespace ns {
    template<typename T>
    const T& min(const T& a, const T& b) {
        return b < a ? b : a;
    }

    struct my_type { int val; };

    template<typename T> struct limits;

    template<>
    struct limits<my_type> {
        static constexpr int get_min() { return 0; }
        // This calls get_min() which should resolve to static member, not ns::min
        static constexpr int get_lowest() { return get_min(); }
    };
}

int main() {
    return ns::limits<ns::my_type>::get_lowest();
}
