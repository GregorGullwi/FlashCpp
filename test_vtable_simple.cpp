// Simple test for vtable initialization
struct Base {
    int x;
    
    Base(int val) : x(val) {}
    
    virtual int getValue() {
        return x;
    }
};

int main() {
    Base b(42);
    int result = b.getValue();
    return result;  // Should return 42
}
