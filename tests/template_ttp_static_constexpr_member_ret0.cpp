// Regression test for: Static constexpr member initializer fails when accessing a member
// via template-template parameter instantiation (e.g., W<int>::id where W is a TTP)
// This verifies that TTP-qualified expressions are properly substituted during template instantiation.

template<typename T>
struct box {
    static constexpr int id = sizeof(T);
};

template<template<typename> class W>
struct probe {
    // This initializer uses W<int>::id where W is a template-template parameter.
    // The expression must be properly substituted to box<int>::id during instantiation.
    static constexpr int sz = W<int>::id;
};

int main() {
    // probe<box>::sz should equal box<int>::id which equals sizeof(int) == 4
    return probe<box>::sz - 4;  // Returns 0 on success
}
