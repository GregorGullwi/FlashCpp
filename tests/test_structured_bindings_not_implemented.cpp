// Test structured bindings - NOW IMPLEMENTED!
// This test verifies that structured bindings work correctly
// with function return values

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
    // This syntax NOW WORKS! ✅
    auto [a, b] = makePair();
    
    return a + b;  // Should return 42
}
