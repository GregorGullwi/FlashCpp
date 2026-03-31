// Phase D fix: default member initializers in nested template structs
// that depend on outer NTTPs must evaluate correctly.
// Bug: needs_default_constructor was never set for nested structs during
// template instantiation, so the trivial default constructor was never
// generated and default member initializers were never applied.
template <int N>
struct Outer {
struct Inner {
int tag = N;
};
};

int main() {
Outer<42>::Inner obj;
return obj.tag;  // Should be 42
}
