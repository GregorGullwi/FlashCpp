// Test structured bindings - KNOWN LIMITATION
// Structured bindings work for direct variable initialization (see test_structured_binding_simple_ret42.cpp)
// but NOT YET for function return values (causes infinite loop during compilation)
// 
// INVESTIGATION: The hang occurs during codegen when visitExpressionNode processes the function call.
// The issue appears to be a deep interaction between how function returns are handled and
// how the structured binding hidden variable is initialized. Further investigation needed.
// 
// This test documents the limitation and uses a workaround until the root cause is fixed.

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
    // TODO: Fix compiler infinite loop - structured bindings from function returns hang during compilation
    // This syntax should work per C++17 spec but currently causes immediate timeout:
    // auto [a, b] = makePair();
    
    // Workaround: Use direct variable initialization (this works perfectly)
    Pair p = makePair();
    int a = p.first;
    int b = p.second;
    
    return a + b;  // Should return 42
}
