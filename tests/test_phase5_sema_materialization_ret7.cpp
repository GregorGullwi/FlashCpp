// Phase 5: sema-owned materialization of template member functions.
// Verifies that member function bodies on template instantiation types are
// materialized during semantic analysis (via ensureMemberFunctionMaterialized)
// so that codegen always consumes an already-materialized declaration.
//
// Three scenarios:
//   1. Direct member function call on a template instantiation (tryRecoverCallDeclFromStructMembers).
//   2. Conversion operator on a template instantiation (tryAnnotateConversion).
//   3. operator() on a callable template instantiation (tryResolveCallableOperatorImpl).
//
// Expected return value: 7

template <typename T>
struct Box {
    T value;
    T get() const { return value; }
    operator T() const { return value; }
    T operator()() const { return value; }
};

int get_val(int x) { return x; }

int main() {
    // Scenario 1: direct member call on template instantiation – sema must
    // materialize Box<int>::get() before codegen visits the call.
    Box<int> b;
    b.value = 3;
    int r1 = b.get();   // should be 3
    if (r1 != 3)
        return 1;

    // Scenario 2: conversion operator on template instantiation – sema must
    // materialize Box<int>::operator int() before codegen emits the cast.
    Box<int> b2;
    b2.value = 2;
    int r2 = get_val(b2);  // implicit Box<int> -> int via operator int()
    if (r2 != 2)
        return 2;

    // Scenario 3: operator() on template instantiation – sema must
    // materialize Box<int>::operator()() before codegen emits the call.
    Box<int> b3;
    b3.value = 2;
    int r3 = b3();   // should be 2
    if (r3 != 2)
        return 3;

    return r1 + r2 + r3;  // 3 + 2 + 2 = 7
}
