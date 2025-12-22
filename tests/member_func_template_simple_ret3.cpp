// Test member function template without implicit special members

class Container {
public:
    int value;
    
    Container() { value = 0; }  // Explicit constructor
    
    template<typename U>
    void insert(U item) {
        value = static_cast<int>(item);
    }
};

int main() {
    Container c;
    c.insert(42);
    c.insert(3.14);
    return c.value;
}

