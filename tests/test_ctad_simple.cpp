// Test class template argument deduction (CTAD) with deduction guides
// This test uses the deduction guide to construct a template without explicit template args

template<typename T>
class Pair {
public:
    T first;
    T second;
    
    Pair(T a, T b) : first(a), second(b) {}
};

// Deduction guide
template<typename T>
Pair(T, T) -> Pair<T>;

int main() {
    // Using CTAD - the compiler should deduce Pair<int> from the arguments
    Pair p(10, 20);  // Should work with CTAD
    
    return p.first + p.second;  // Should return 30
}
