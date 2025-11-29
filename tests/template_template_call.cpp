// Test calling template template function
template<template<typename> class Container, typename T>
void test_func(Container<T> arg) {
}

template<typename T>
class Vector {
public:
    T data;
};

int main() {
    Vector<int> v;
    test_func(v);
    return 0;
}
