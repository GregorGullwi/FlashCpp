// Test member struct template partial specialization
// This pattern is used in <type_traits> at line 1841

class TestClass {
protected:
    // Primary template
    template<typename...> struct List { };

    // Partial specialization
    template<typename T, typename... Rest>
    struct List<T, Rest...> : List<Rest...> {
    };
};

int main() {
    // Instantiate template - the key is that partial specialization compiles
    TestClass::List<int, char, float> list;
    return 1;  // Return 1 to indicate success
}
