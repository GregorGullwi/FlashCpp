// Regression: C-style variadic arguments must not deduce a template parameter pack.
// This also covers the empty-pack substitution path exposed once the erroneous
// codegen-time pack fill was removed.

template<typename... T>
int c_varargs_count(int, ...) {
return sizeof...(T);
}

int main() {
return c_varargs_count<>(1, 2, 3);
}
