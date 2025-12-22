// Variadic template with member expansion test
// This test checks if we can expand parameter packs into member variables

template<typename... Args>
struct Tuple {
    // For now, empty - pack expansion in members not yet implemented
    // Future: Args... members; should expand to multiple member variables
};

// Specialization for single element (non-variadic workaround)
template<typename T>
struct Tuple<T> {
    T value;
};

// Specialization for two elements
template<typename T1, typename T2>
struct Tuple<T1, T2> {
    T1 first;
    T2 second;
};

int main() {
    // Test empty tuple
    Tuple<> empty;
    
    // Test single-element tuple (uses first specialization)
    Tuple<int> single;
    single.value = 42;
    
    // Test two-element tuple (uses second specialization)
    Tuple<int, float> pair;
    pair.first = 10;
    pair.second = 3.14f;
    
    return pair.first;  // Should return 10
}
