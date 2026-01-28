// Test: Template features that need to be implemented
// 1. Nested types ✓ WORKING
// 2. Static members ⚠️ PARTIALLY WORKING (parses but access not supported)
// 3. Type aliases (using) ✓ WORKING (non-template context only)

template<typename T>
struct Container {
    // Nested type ✓ WORKS!
    struct Iterator {
        T* ptr;
    };
    
    // Type alias - works in non-template context, not yet in template instantiation context
    // using value_type = T;
    
    // Static member ⚠️ parses correctly but cannot be accessed via scope resolution yet
    static const int size = 10;
    
    T data;
};

int main() {
    // Test nested type - ✓ WORKS!
    Container<int>::Iterator it;
    it.ptr = nullptr;  // ✓ nullptr support works!
    
    // Test type alias - limitation: doesn't work in template instantiation context yet
    // Container<int>::value_type x = 42;
    
    // Test static member - limitation: parses but cannot access via ::
    // int x = Container<int>::size;  // Would fail in IRConverter
    
    return 0;
}
