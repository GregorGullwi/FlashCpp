// Test individual values

template<int N>
struct Container {
    int data[N];
    
    int get_size() {
        return N;
    }
    
    int double_size() {
        return N * 2;
    }
    
    bool is_large() {
        return N > 10;
    }
};

int main() {
    Container<5> c;
    bool large = c.is_large();
    
    // If large is false (0), this should return 0
    // If large is true (1), this should return 1  
    return large ? 1 : 0;
}
