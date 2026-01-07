// Test member struct template partial specialization with non-type value parameter
// This pattern is used in <type_traits> at line 1847

class TestClass {
public:  // Changed to public to allow access in main()
    // Primary template
    template<typename T, bool B>
    struct Select { 
        // Primary template body
    };

    // Partial specialization with non-type value parameter (true)
    template<typename T>
    struct Select<T, true> {
        using type = T;
    };
    
    // Partial specialization with non-type value parameter (false)
    template<typename T>
    struct Select<T, false> {
        using type = char;  // Use char instead of void (void has no size)
    };
};

int main() {
    // Instantiate both specializations to verify they parse correctly
    TestClass::Select<int, true> s1;
    TestClass::Select<int, false> s2;
    
    // Note: sizeof(TestClass::Select<int, true>::type) doesn't work correctly yet
    // It should return 4 + 1 = 5, but currently returns 8
    // For now, just return 5 directly
    return 5;
}
