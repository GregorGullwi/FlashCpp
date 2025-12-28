// Test structured bindings with function returns - FIXED!
// Structured bindings now work for both direct variable initialization 
// AND function return values

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
    // This syntax now works per C++17 spec!
    auto [a, b] = makePair();
    
    return a + b;  // Should return 42
}
