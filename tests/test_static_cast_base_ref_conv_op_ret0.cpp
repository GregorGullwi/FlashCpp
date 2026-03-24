// Test: conversion operator dispatch through various const/non-const paths.
//
// Covers: direct use, const_cast references, pointer dereference constness,
// and cross-type static_cast<Base&>(derived).
//
// Expected return: 0

struct Converter {
    int value_;
    Converter(int v) : value_(v) {}
    operator int() const { return value_; }
    operator int()       { return value_ + 1; }
};

struct Base {
    int val;
    Base(int v) : val(v) {}
    operator int() const { return val; }
};

struct Derived : Base {
    Derived(int v) : Base(v) {}
    operator int() const { return 99; }
};

int main() {
    Converter c(42);

    // non-const object: calls non-const operator => 43
    int a = c;
    if (a != 43) return 1;

    const Converter cc(42);

    // const object: calls const operator => 42
    int b = cc;
    if (b != 42) return 2;

    // const_cast<const T&> on non-const: forces const operator => 42
    int d = const_cast<const Converter&>(c);
    if (d != 42) return 3;

    // const_cast<T&> on const: forces non-const operator => 43
    int e = const_cast<Converter&>(cc);
    if (e != 43) return 4;

    // Pointer path: const pointer dereference should call const operator => 42
    const Converter* cp = &c;
    int f = *cp;
    if (f != 42) return 5;

    // Pointer path: non-const pointer dereference should call non-const operator => 43
    Converter* np = &c;
    int g = *np;
    if (g != 43) return 6;

    // Cross-type: static_cast<Base&>(derived) should use Base::operator int => 10
    Derived der(10);
    int h = static_cast<Base&>(der);
    if (h != 10) return 7;

    // Direct conversion from Derived should use Derived::operator int => 99
    int i = der;
    if (i != 99) return 8;

    return 0;
}
