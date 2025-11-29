// Test that member access result doesn't corrupt 'this' pointer

template<typename T>
struct Container;

template<typename T>
struct Container<T*> {
    int value;
    
    int getValue() {
        return value;  // Should return 42
    }
};

int main() {
    Container<int*> c;
    c.value = 42;
    return c.getValue();  // Should return 42, not garbage
}
