// Test inherited non-static member template function lookup
// This pattern is used extensively in <type_traits>

struct Base {
    template<typename T>
    int get_value() {
        return 42;
    }
};

struct Derived : Base {
    int test() {
        return get_value<int>();  // Lookup through inheritance
    }
};

int main() {
    Derived d;
    return d.test();
}
