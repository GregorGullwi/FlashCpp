// Test progressive addition

template<int N>
struct Container {
    int data[N];
    int get_size() { return N; }
    int double_size() { return N * 2; }
    bool is_large() { return N > 10; }
};

int main() {
    Container<5> c;
    int size = c.get_size();  // 5
    int doubled = c.double_size();  // 10
    bool large = c.is_large();  // false (0)
    
    // Test each intermediate value
    int a = size + doubled;  // Should be 15
    int b = large ? 1 : 0;    // Should be 0
    
    // Test: should give 15
    return a + b;
}
