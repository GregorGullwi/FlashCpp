// Test template member functions

template<int N>
struct Container {
    int get_value() {
        return N;
    }
    
    int get_double() {
        return N * 2;
    }
};

int main() {
    Container<5> c;
    int a = c.get_value();
    int b = c.get_double();
    
    // Should return 5 + 10 = 15
    return a + b;
}
