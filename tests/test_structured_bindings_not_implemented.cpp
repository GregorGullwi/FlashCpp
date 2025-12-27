// Test structured bindings - EXPECTED TO FAIL
// This test documents that structured bindings are not yet implemented
// despite __cpp_structured_bindings being defined

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
    // This syntax is NOT supported yet
    // Parser error: "Missing identifier: a"
    // auto [a, b] = makePair();
    
    // Workaround: Manual extraction
    Pair p = makePair();
    int a = p.first;
    int b = p.second;
    
    return a + b;  // Should return 42
}
