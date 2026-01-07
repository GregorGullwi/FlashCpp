// Test member struct template partial specialization with non-type value parameter
// This pattern is used in <type_traits> at line 1847

class TestClass {
protected:
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
        using type = void;
    };
};

int main() {
    // Instantiate both specializations to verify they parse correctly
    // The key test is that the partial specializations with non-type value parameters compile
    TestClass::Select<int, true> s1;
    TestClass::Select<int, false> s2;
    
    // Return 5 = sizeof(int) + sizeof(void) conceptually
    return 5;
}
