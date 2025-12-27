// Test basic structured binding with a simple struct
// Expected return: 42

struct Pair {
    int first;
    int second;
};

int main() {
    Pair p;
    p.first = 10;
    p.second = 32;
    
    // Structured binding: auto [a, b] = p;
    auto [a, b] = p;
    
    return a + b;  // Should return 42
}
