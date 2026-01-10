// Test inherited member template function lookup
// This pattern is used extensively in <type_traits>

struct Base {
    template<typename T>
    static int get_value() {
        return 42;
    }
};

struct Derived : Base {
    // Should be able to call inherited template function
    using type = int;
    
    int test() {
        return get_value<int>();  // Lookup through inheritance
    }
};

int main() {
    Derived d;
    return d.test();
}
