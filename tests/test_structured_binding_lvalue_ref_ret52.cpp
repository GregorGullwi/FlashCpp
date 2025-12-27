// Test structured bindings with lvalue reference
// Expected return: 52

struct Pair {
    int first;
    int second;
};

int main() {
    Pair p;
    p.first = 10;
    p.second = 32;
    
    // Structured binding with lvalue reference: auto& [a, b] = p;
    auto& [a, b] = p;
    
    // Modify through reference
    a = 20;
    
    // Should see the modification in the original
    return p.first + p.second;  // Should return 20 + 32 = 52
}
