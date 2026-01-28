// Simplified template template parameter test
template<template<typename> class Container, typename T>
void test_func(Container<T> arg) {
}

int main() {
    return 0;
}
