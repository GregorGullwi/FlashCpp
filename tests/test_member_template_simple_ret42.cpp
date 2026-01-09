// Test member template function lookup (no inheritance)

struct Container {
    template<typename T>
    int get_value() {
        return 42;
    }
    
    int test() {
        return get_value<int>();  // Should work
    }
};

int main() {
    Container c;
    return c.test();
}
