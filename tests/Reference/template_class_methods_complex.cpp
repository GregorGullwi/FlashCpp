// Class template with member functions that use template types
// Tests if template parameter substitution works in function bodies
template<typename T>
class Container {
public:
    T value;
    
    void set(T v) {
        value = v;
    }
    
    T get() {
        return value;
    }
};

int main() {
    Container<int> c;
    c.set(42);
    int x = c.get();
    return x - 42;
}
