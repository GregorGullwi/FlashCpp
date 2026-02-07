// Test: Destructor in full template specialization
// Validates parsing of ~ClassName() in template<> struct ClassName<Type> {}
template<typename T>
struct Container {
    int value;
};

template<>
struct Container<void> {
    int value;
    Container() = default;
    ~Container() = default;
};

int main() {
    return sizeof(Container<void>);  // 4 (sizeof int)
}
