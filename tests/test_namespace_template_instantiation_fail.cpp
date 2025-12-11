// Minimal test case for namespace-qualified template instantiation bug
// This test demonstrates that templates in namespaces work fine on their own,
// but fail when actually instantiated with namespace-qualified names.

namespace my_ns {
    template<typename T>
    struct Wrapper {
        static constexpr int value = 42;
    };
}

int main() {
    // This line causes "Failed to parse top-level construct" error
    return my_ns::Wrapper<int>::value - 42;
}
