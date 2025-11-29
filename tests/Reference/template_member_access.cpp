// Test member function template with member variable access

class Container {
public:
    int value;
    
    template<typename U>
    void set(U item) {
        value = 42;
    }
};

int main() {
    Container c;
    c.set(10);
    return c.value;
}
