class Container {
public:
    template<typename U>
    void insert(U item) {
        42;
    }
};

int main() {
    Container c;
    c.insert(5);
    return 0;
}
