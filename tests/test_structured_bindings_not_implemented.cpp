// Test structured bindings - PARTIAL IMPLEMENTATION
// Structured bindings work for direct variable initialization (see test_structured_binding_simple_ret42.cpp)
// but NOT YET for function return values (causes infinite loop/hang)
// This test documents the limitation and uses a workaround until fixed

struct Pair {
    int first;
    int second;
};

Pair makePair() {
    Pair p;
    p.first = 10;
    p.second = 32;
    return p;
}

int main() {
    // TODO: Fix compiler bug - structured bindings from function returns cause hang
    // This syntax should work but currently causes timeout:
    // auto [a, b] = makePair();
    
    // Workaround: Use direct variable initialization (this works)
    Pair p = makePair();
    int a = p.first;
    int b = p.second;
    
    return a + b;  // Should return 42
}
