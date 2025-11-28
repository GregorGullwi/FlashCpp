// Test explicit template instantiation
template<typename T>
class Container {
public:
    T value;
    
    void set(T val) {
        value = val;
    }
};

// Explicit template instantiation
template void Container<int>::set(int);

int main() {
    Container<int> c;
    c.set(42);
    return c.value;
}
