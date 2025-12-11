// Test partial specialization with inheritance
// This pattern is used extensively in <type_traits> and other standard headers

template<typename T> 
struct Base { 
};

// Full specialization
template<> 
struct Base<int> { 
    int x;
};

// Partial specialization with inheritance - pattern from standard library
template<typename T> 
struct Base<const T> : Base<T> { 
};

int main() {
    // Test that partial specialization with inheritance works
    // Base<const int> should inherit from Base<int>
    Base<const int> b;
    return 0;
}
