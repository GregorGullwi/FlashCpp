#include "doctest.h"
#include <cstdint>

// This test file exercises template-template parameter support in FlashCpp.
// It is intentionally minimal and mirrors the style of existing template tests.
//
// Goals:
// - Verify the parser accepts template template parameters:
//       template<template<typename> class Container>
// - Verify that simple instantiations with such parameters type-check and run
//   under the doctest-based FlashCpp test harness, without depending on <vector>.
//
// Constraints / Design:
// - Use only `template<typename> class` as the inner parameter form.
// - Use a simple Container-like type instead of std::vector.
// - Keep semantics simple so failures indicate template machinery issues,
//   not runtime logic errors.

// A very small container-like template used for testing.
// It does NOT allocate: it just stores a pointer to an external object.
// This keeps codegen trivial and focused on template behavior.
template<typename T>
struct SimpleVector {
    T* data_;
    int size_;

    SimpleVector()
        : data_(nullptr),
          size_(0) {
    }

    // Set as a single-element "view" pointing at v.
    void set_single(const T& value) {
        // For test purposes, alias the input; lifetime is controlled in the tests.
        data_ = &const_cast<T&>(value);
        size_ = 1;
    }

    T& front() {
        return *data_;
    }

    const T& front() const {
        return *data_;
    }

    int size() const {
        return size_;
    }
};

// Primary template that takes a template-template parameter and uses it
// in a static member template function.
template<template<typename> class Container>
struct Wrapper {
    template<typename T>
    static T get_first(const T& v) {
        Container<T> c;
        c.set_single(v);
        return c.front();
    }
};

// Variant that has state and uses the Container in a member template.
template<template<typename> class Container>
struct WrapperWithMember {
    int factor;

    WrapperWithMember()
        : factor(1) {
    }

    template<typename T>
    T multiply_first(const T& v) const {
        Container<T> c;
        c.set_single(static_cast<T>(v * static_cast<T>(factor)));
        return c.front();
    }
};

TEST_CASE("TemplateTemplateParameter: basic Wrapper with SimpleVector") {
    int value = 42;
    int result = Wrapper<SimpleVector>::get_first(value);
    CHECK(result == 42);
}

TEST_CASE("TemplateTemplateParameter: WrapperWithMember using SimpleVector") {
    WrapperWithMember<SimpleVector> w;
    w.factor = 3;

    int value = 7;
    int result = w.multiply_first(value);
    CHECK(result == 21);
}