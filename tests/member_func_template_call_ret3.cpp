// Test calling a member function template

class Container {
public:
    int value;
    
    template<typename U>
    void insert(U item) {
        value = static_cast<int>(item);
    }
};

int main() {
    Container c;
    c.insert(42);        // Should instantiate insert<int>
    c.insert(3.14);      // Should instantiate insert<double>
    return c.value;
}

