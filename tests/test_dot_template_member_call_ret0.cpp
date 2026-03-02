// Test: obj.template member<T>() syntax parsing
// The 'template' keyword disambiguates dependent member template calls
// Verifies parsing accepts the 'template' disambiguator after . and ->

template<typename T>
struct Container {
    T val;
    template<typename U>
    U cast() const { return static_cast<U>(val); }
};

int main() {
    Container<int> c;
    c.val = 42;
    return c.template cast<int>() - 42;
}
