// Test that structured bindings reject invalid modifiers
// This should fail to compile

int main() {
    int arr[2] = {1, 2};
    
    // This should error: structured bindings cannot be static
    static auto [a, b] = arr;
    
    return a + b;
}
