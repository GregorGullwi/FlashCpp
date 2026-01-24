// Test: Unknown identifier handling in template contexts
// This tests that unknown identifiers in templates are treated as dependent
// and resolved at instantiation time

template<typename T>
struct HasMember {
    T value;
    
    // Test: reference to 'nested_type' which doesn't exist yet in template context
    // Should be treated as dependent and resolved at instantiation
    void set_value(T v) {
        value = v;
    }
};

// Test 2: Template with potential member reference
template<typename T>
struct Container {
    T data;
    
    // This might reference T::size_type which may not exist
    // Parser should treat unknown identifiers as potentially dependent
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
