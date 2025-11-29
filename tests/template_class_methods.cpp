// Class template with member functions
// Tests instantiation of member functions
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
    return x;
}

