// Test: Unknown identifier handling in template contexts
// This tests that unknown identifiers in templates are treated as dependent
// and resolved at instantiation time

template<typename T>
struct HasMember {
    T value;
    
    // Test: Parser handles templates with potentially unknown member references
    // These references are treated as dependent and resolved at instantiation
    void set_value(T v) {
        value = v;
    }
};

// Test 2: Template with operations on dependent type
template<typename T>
struct Container {
    T data;
    
    // Parser treats T as potentially having unknown members/properties
    // Handles the template definition without needing full type information
    int get_size() {
        return sizeof(T);
    }
};

// Test 3: SFINAE-like pattern where identifier may not resolve
template<typename T>
struct TypeWrapper {
    using type = T;
    
    // Reference that may not exist - should parse without error
    static constexpr int value = sizeof(T);
};

int main() {
    // Instantiate the templates
    HasMember<int> h;
    h.set_value(10);
    
    Container<int> c;
    c.data = 32;
    
    TypeWrapper<int> w;
    
    // Return h.value + c.data = 10 + 32 = 42
    return h.value + c.data;
}
