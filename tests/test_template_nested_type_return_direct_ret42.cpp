// Regression test: template member function returning a nested struct must not
// enter the converting-constructor codegen fallback.  The TypeIndex carried by
// the ConstructorCallNode and the TypeIndex in the function's return-type
// specifier referred to the same logical type via different TypeIndex slots
// (lazy-instantiated struct vs. TypeAlias created by register_nested_class_aliases).
// Fixed in isSameTypeConstructorCallInitialization (sema) and the same-name
// normalization in visitReturnStatementNode (codegen).

template <int N>
struct Container {
struct Entry {
int value;
};

Entry make_entry(int v) {
Entry e;
e.value = v;
return e;
}

Entry make_direct() {
return Entry{N};
}
};

int main() {
Container<21> c;
auto e1 = c.make_entry(21);   // value=21
auto e2 = c.make_direct();    // value=21 (N=21)
// e1.value + e2.value = 42
return e1.value + e2.value;
}
