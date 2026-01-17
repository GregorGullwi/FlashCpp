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
    
    // Function that creates a local variable of type T
    T add(T x) {
        T result = value;
        result = result + x;
        return result;
    }
};

int main() {
    Container<int> c;
    c.set(10);
    int x = c.add(32);  // Should return 42
    return x;
}
