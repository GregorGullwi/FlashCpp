// Test member struct template partial specialization
// This pattern is used in <type_traits> at line 1841

class TestClass {
protected:
    // Primary template
    template<typename...> struct List { 
        static constexpr int size = 0;
    };

    // Partial specialization with static constexpr member
    template<typename T, typename... Rest>
    struct List<T, Rest...> : List<Rest...> {
        static constexpr int size = 1;
    };
};

int main() {
    // Instantiate template - the static constexpr members now parse correctly
    TestClass::List<int, char, float> list;
    return 1;  // Return 1 for the size value
}
