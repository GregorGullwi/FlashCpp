// Test member call + ternary

template<int N>
struct Container {
    int data[N];
    int get_size() { return N; }
    bool is_large() { return N > 10; }
};

int main() {
    Container<5> c;
    int size = c.get_size();  // 5
    bool large = c.is_large();  // false (0)
    
    // Test: should give 5 + 0 = 5
    return size + (large ? 1 : 0);
}
