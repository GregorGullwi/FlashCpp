// Test member function template

struct Container {
    int value;
    
    template<typename U>
    void insert(U item) {
        value = item;
    }
};

int main() {
    Container c;
    c.value = 0;
    c.insert(42);
    return c.value;
}
