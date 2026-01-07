// Test member struct template partial specialization with non-type value parameter
// This pattern is used in <type_traits> at line 1847

class TestClass {
protected:
    // Primary template
    template<int N, typename T, bool B>
    struct Select { };

    // Partial specialization with non-type value parameter (true)
    template<int N, typename T>
    struct Select<N, T, true> {
        using type = T;
    };
    
    // Partial specialization with non-type value parameter (false)
    template<int N, typename T>
    struct Select<N, T, false> {
        using type = void;
    };
};

int main() {
    TestClass::Select<5, int, true> s1;
    TestClass::Select<5, int, false> s2;
    return 0;
}
