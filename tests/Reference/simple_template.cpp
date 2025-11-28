// Simpler test - template function that just returns a value
class Container {
public:
    int value;
    
    template<typename U>
    int get(U item) {
        return 42;
    }
};

int main() {
    Container c;
    return c.get(5);
}
