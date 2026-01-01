// Test that tuple-like types are detected via std::tuple_size
// When std::tuple_size<E> is specialized for the type E, the compiler recognizes
// it as a tuple-like type and validates that the number of bindings matches.
// Since the full get<>() protocol isn't implemented yet, it falls back to
// aggregate decomposition for types with public members.
// Expected return: 42

namespace std {
    template<typename T>
    struct tuple_size;
}

struct MyPair {
    int first;
    int second;
};

// Specialize std::tuple_size for MyPair - this triggers tuple-like detection
namespace std {
    template<>
    struct tuple_size<MyPair> {
        static constexpr size_t value = 2;
    };
}

int main() {
    MyPair p;
    p.first = 10;
    p.second = 32;
    
    // The compiler detects tuple_size<MyPair>, validates size matches 2,
    // and falls back to aggregate decomposition (since get<>() not fully implemented)
    auto [a, b] = p;
    
    return a + b;  // Should return 42 (10 + 32)
}
