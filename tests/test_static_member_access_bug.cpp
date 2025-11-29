// Test: Static member access in template instantiations

template<typename... Args>
struct Tuple {
    static const int size = sizeof...(Args);
};

int main() {
    // Access static member of empty pack instantiation
    int empty_size = Tuple<>::size;      // Should be 0
    
    return empty_size;  // Should return 0
}
