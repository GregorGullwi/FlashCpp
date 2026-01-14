// Test for namespace-qualified variable template lookup inside function templates
// This test verifies that variable templates defined in a namespace can be used
// from within function templates in the same namespace.

namespace ns {
    template<typename _Tp>
    inline constexpr bool is_simple_v = true;
    
    // Use the variable template inside a function template
    template<typename T>
    constexpr bool test() {
        return is_simple_v<T>;
    }
}

int main() {
    // Direct use from outside namespace
    bool r1 = ns::is_simple_v<int>;
    
    // Use through function template
    bool r2 = ns::test<int>();
    
    return (r1 && r2) ? 0 : 1;
}
