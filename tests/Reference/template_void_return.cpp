class Container {
public:
    int value;
    
    template<typename U>
    void insert(U item) {
        return;
    }
};

int main() {
    Container c;
    c.insert(42);
    return 0;
}
