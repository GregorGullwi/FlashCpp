// Phase 5 test: Maximal munch for >> in nested templates
// Tests that Foo<Bar<int>> is correctly parsed, not as Foo<(Bar<int) >> (something)>
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
    // Test 1: Simple nested template instantiation with >> splitting
    // This tests that >> is split into > > during template argument parsing
    ns::Outer<ns::Inner<int>> nested;
    nested.inner.value = 42;
    
    return nested.inner.value;  // 42
}
