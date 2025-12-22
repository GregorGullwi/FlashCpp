class Container {
public:
    int value;
    
    template<typename U>
    void insert(U item) {
        int x = 5;
        value = x;
    }
};

int main() {
    Container c;
    c.insert(42);
    return c.value;
}
