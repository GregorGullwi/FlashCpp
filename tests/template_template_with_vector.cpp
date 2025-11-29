// Template template parameter test with Vector
template<template<typename> class Container, typename T>
void test_func(Container<T> arg) {
}

template<typename T>
class Vector {
public:
    T data;
};

int main() {
    return 0;
}
