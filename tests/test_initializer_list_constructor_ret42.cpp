// Test for struct constructor with member initialization
// Simplified test without initializer_list complexity

class Container {
public:
    int value;
    
    Container(int v) : value(v) {}
};

int main() {
    Container c(42);
    return c.value;  // Should return 42
}
