// Test that template specializations with deleted default constructors
// cannot be instantiated with empty initializer lists
// This is a _fail test - it should fail to compile

template<typename T>
struct Foo {
    int value;
};

// Specialization with deleted default constructor
template<typename T>
struct Foo<T*> {
    Foo() = delete;
    int value = 42;
};

int main() {
    // This should be a compile error - attempting to use deleted default constructor
    Foo<int*> f{};
    return 0;
}
