// Test: template forward declaration followed by full definition
// The full definition should replace the forward declaration so that
// members, base classes, and member functions are visible during instantiation.

template<typename T>
struct Wrapper;  // forward declaration

template<typename T>
struct Wrapper {  // full definition
    T value;
    T get() const { return value; }
};

int main() {
    Wrapper<int> w;
    w.value = 42;
    return w.get() == 42 ? 0 : 1;
}
