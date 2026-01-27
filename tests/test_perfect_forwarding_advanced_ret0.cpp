// Advanced perfect forwarding test - demonstrates real-world usage

// Variadic function that forwards to another function
template<typename... Args>
void print_wrapper(Args&&... args) {
    // Would forward to actual print function
}

// Function that takes multiple parameters of different types
void process(int a, double b, char c) {
    // Just accept the arguments
}

// Forwarding function
template<typename... Args>
void forward_to_process(Args&&... args) {
    process(args...);  // Forward arguments
}

int main() {
    // Test 1: Perfect forwarding with different types
    print_wrapper(100, 200, 300);
    print_wrapper(1, 2.5, 'a');
    
    // Test 2: Forward to specific function
    forward_to_process(42, 3.14, 'x');
    
    return 0;
}
