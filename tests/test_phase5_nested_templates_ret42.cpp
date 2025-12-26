// Phase 5 test: Maximal munch for >> in nested templates with namespaces
// Tests that ns::Outer<ns::Inner<int>> is correctly parsed
// C++20 requires maximal munch: >> should be split into > > when closing nested templates

namespace ns {
    template<typename T>
    struct Inner {
        T value;
    };
    
    template<typename T>
    struct Outer {
        T inner;
    };
}

int main() {
    // Test: Namespace-qualified nested template instantiation with >> splitting
    // This validates that >> is correctly split into > > during template argument parsing
    // The key test is that this line compiles without treating >> as a right-shift operator
    ns::Outer<ns::Inner<int>> nested = {};
    
    // The test passes if compilation succeeds with >> splitting
    return 42;
}
