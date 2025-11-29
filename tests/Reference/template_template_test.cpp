// Test template template parameters
// This tests the basic parsing and AST construction for template template parameters

template<template<typename> class Container, typename T>
void test_template_template(Container<T> arg) {
    // Simple function that takes a container of T
}

template<typename T>
class Vector {
public:
    T data;
};

int main() {
    Vector<int> v;
    v.data = 42;

    // This should instantiate test_template_template with Container=Vector, T=int
    test_template_template(v);

    return 0;
}