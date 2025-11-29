// Standard C++ tuple implementation approach (simplified)
// Uses recursive inheritance, not pack expansion in members

// Base case: empty tuple
template<typename... Args>
struct Tuple;

template<>
struct Tuple<> {
    static const int size = 0;
};

// Recursive case: first element + rest
template<typename First, typename... Rest>
struct Tuple<First, Rest...> : Tuple<Rest...> {
    First value;
    static const int size = 1 + sizeof...(Rest);
    
    Tuple() = default;
    Tuple(First f, Rest... r) : Tuple<Rest...>(r...), value(f) {}
};

int main() {
    Tuple<> empty;
    Tuple<int> single;
    Tuple<int, float> pair;
    
    // Access via inheritance chain
    single.value = 42;
    pair.value = 10;
    
    return single.value - 10;  // 42 - 10 = 32
}
