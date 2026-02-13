// Test that the compiler handles codegen errors gracefully
// Previously, codegen errors caused assert(false) which hung the compiler
// Now they throw std::runtime_error which is caught and allows clean exit

template<typename T>
struct Wrapper {
    T value;
    
    T get() const { return value; }
};

int main() {
    Wrapper<int> w;
    w.value = 42;
    return w.get();
}
