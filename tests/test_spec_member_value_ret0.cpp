// Test basic member access and comparison

template<typename T>
struct Container;

template<typename T>
struct Container<T*> {
    int value;
    
    void set(int v) {
        value = v;
    }
    
    int check() {
        if (value == 42) {
            return 0;
        }
        return 1;
    }
};

int main() {
    Container<int*> c;
    c.set(42);
    // Should return 0
    return c.check();
}
