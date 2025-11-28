// Simplest possible test - no CRT, just a function that returns 42

class Container {
public:
    int value;
    
    template<typename U>
    void insert(U item) {
        value = item;
    }
};

extern "C" int simple_main() {
    Container c;
    c.value = 0;
    c.insert(42);
    return c.value;
}
