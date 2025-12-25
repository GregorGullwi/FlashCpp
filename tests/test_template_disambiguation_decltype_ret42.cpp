// Test: C++20 Template Argument Disambiguation - Decltype
// Tests that decltype with qualified identifiers and template arguments works correctly

namespace ns {
    template<typename T>
    struct Helper {
        using type = int;
    };
    
    template<typename T>
    int getValue() {
        return 42;
    }
}

// Test: decltype with template arguments in base class
template<typename T>
struct Base : decltype(ns::getValue<T>()) {
};

// Simplified test - just verify parsing works
int main() {
    return 42;
}
