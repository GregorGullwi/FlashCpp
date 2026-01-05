// Test member template function with braced initializer in default parameter
// This pattern appears in <type_traits> line 1326

struct test_struct {
    // Forward declare a template helper function
    template <typename T>
    static void helper(const T&);
    
    // Use the helper in a decltype default parameter with braced initializer
    template <typename T>
    static int test_function(const T&, 
                              decltype(helper<const T&>({}))* = 0) {
        return 42;
    }
};

int main() {
    int x = 5;
    return test_struct::test_function(x) == 42 ? 0 : 1;
}
