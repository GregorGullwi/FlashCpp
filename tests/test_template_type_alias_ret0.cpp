// Test case: Type aliases in template context
// Container<int>::value_type should resolve to int

template<typename T>
struct Container {
    using value_type = T;
    using size_type = int;
    
    T data;
};

int main() {
    Container<int>::value_type x = 42;        // Should be int
    Container<double>::value_type y = 3.14;   // Should be double
    Container<int>::size_type size = 10;      // Should be int
    
    return 0;
}
