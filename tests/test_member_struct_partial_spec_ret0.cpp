// Test member struct template partial specialization
// This pattern is used in <type_traits> at line 1841

class TestClass {
protected:
    // Primary template
    template<typename...> struct List { };

    // Partial specialization - this is what's failing
    template<typename T, typename... Rest>
    struct List<T, Rest...> : List<Rest...> {
        static constexpr int size = 1;
    };
};

int main() {
    return 0;
}
