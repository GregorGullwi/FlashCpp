// Test full tuple-like structured binding protocol with get<N>() - both ints
// Expected return: 42 (10 + 32)

namespace std {
    template<typename T>
    struct tuple_size;
    
    template<unsigned long I, typename T>
    struct tuple_element;
}

struct MyPair {
    int first;
    int second;
};

// Specialize tuple_size
namespace std {
    template<>
    struct tuple_size<MyPair> {
        static constexpr unsigned long value = 2;
    };
    
    template<>
    struct tuple_element<0, MyPair> {
        using type = int;
    };
    
    template<>
    struct tuple_element<1, MyPair> {
        using type = int;
    };
}

// Define get<> function template
template<unsigned long I>
int get(const MyPair& p);

// Explicit specializations for get<0> and get<1>
template<>
int get<0>(const MyPair& p) {
    return p.first;
}

template<>
int get<1>(const MyPair& p) {
    return p.second;
}

int main() {
    MyPair p;
    p.first = 10;
    p.second = 32;
    
    // This should use the tuple-like protocol:
    auto [a, b] = p;
    
    return a + b;  // Should return 42
}
