// Template template parameter test with Vector instantiation
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
    return 0;
}
