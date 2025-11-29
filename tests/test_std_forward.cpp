// Test std::forward intrinsic for perfect forwarding

// Utility function that uses std::forward
template<typename T>
T&& my_forward(T&& arg) {
    return std::forward<T>(arg);
}

// Function that accepts forwarded arguments
template<typename... Args>
void forward_all(Args&&... args) {
    // In real usage, would forward to another function
    // For now, just accept the arguments
}

// Helper function
void consume(int a, double b, char c) {
    // Just consume
}

// Simpler factory-style function using std::forward
template<typename T, typename U, typename V>
void make_and_consume_simple(T&& a, U&& b, V&& c) {
    consume(std::forward<T>(a), std::forward<U>(b), std::forward<V>(c));
}

int main() {
    // Test 1: Simple forward
    int x = 42;
    int&& result = my_forward(x);
    
    // Test 2: Forward in variadic context
    forward_all(1, 2.5, 'a');
    
    // Test 3: Factory pattern with std::forward (non-variadic version)
    make_and_consume_simple(100, 3.14, 'z');
    
    return 0;
}
