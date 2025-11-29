// Test: Type alias used within the same instantiated template

template<typename T>
struct Container {
    using value_type = T;
    
    value_type data;  // This should work - uses the alias within the template
};

int main() {
    Container<int> c;
    c.data = 42;
    return 0;
}
