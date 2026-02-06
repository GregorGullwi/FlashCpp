// Test: Destructor in full template specialization
// Validates parsing of ~ClassName() in template<> struct ClassName<Type> {}
template<typename T>
struct Container {};

template<>
struct Container<void> {
    Container() = default;
    ~Container() = default;
};

int main() {
    Container<void> c;
    return 0;
}
