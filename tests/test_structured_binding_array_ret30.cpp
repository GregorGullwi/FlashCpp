// Test structured bindings with arrays
// Expected return: 30

int main() {
    int arr[3] = {10, 15, 5};
    
    // Structured binding: auto [a, b, c] = arr;
    auto [a, b, c] = arr;
    
    return a + b + c;  // Should return 30
}
