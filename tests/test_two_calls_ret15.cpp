// Test without ternary

template<int N>
struct Container {
    int data[N];
    int get_size() { return N; }
    int double_size() { return N * 2; }
};

int main() {
    Container<5> c;
    int size = c.get_size();  // 5
    int doubled = c.double_size();  // 10
    
    // Test: should give 15
    return size + doubled;
}
