// Test variadic function templates with parameter packs

template<typename... Args>
void func(Args... args) {
    // This should expand to: T0 arg0, T1 arg1, T2 arg2, ...
    // For now, just a simple function body
}

int main() {
    // Test with zero arguments
    func();
    
    // Test with one argument
    func(42);
    
    // Test with multiple arguments
    func(1, 2.5, 'a');
    
    return 0;
}
