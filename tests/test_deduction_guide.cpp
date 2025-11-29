// Test template deduction guides (C++17 CTAD)
// Deduction guides allow class template argument deduction from constructor arguments

template<typename T>
class Wrapper {
public:
    T value;
    
    Wrapper(T v) : value(v) {}
};

// Deduction guide: when constructing Wrapper with a T, deduce Wrapper<T>
template<typename T>
Wrapper(T) -> Wrapper<T>;

int main() {
    // With deduction guide, we can write:
    // Wrapper w(42);  // Deduces Wrapper<int>
    // instead of:
    Wrapper<int> w(42);
    
    return w.value;
}
