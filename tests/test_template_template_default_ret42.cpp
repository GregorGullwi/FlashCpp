// Test that template template parameter defaults parse correctly without error
// The parser previously caused a parse error on the '=' token
template<typename T>
struct MyContainer {
    T value;
};

// Template with template template parameter with default - should parse without error
template<typename T, template<typename> class Container = MyContainer>
struct Wrapper {
    T data;
};

// Explicit usage (not relying on default) to verify basic functionality
template<typename T, template<typename> class Container>
struct Holder {
    T data;
};

int main() {
    Wrapper<int> w;
    w.data = 42;
    return w.data;
}
