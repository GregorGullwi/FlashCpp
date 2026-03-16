// Test that ambiguous operator() overloads are rejected.
// Two overloads with cross-wise parameter types: neither is strictly better
// for arguments (int, int). A conforming compiler must reject this as ambiguous.
//
// Overload 1: operator()(int, double)  → arg 0 exact, arg 1 conversion
// Overload 2: operator()(double, int)  → arg 0 conversion, arg 1 exact
// Call:        f(1, 2)                 → neither overload is strictly better

struct Ambiguous {
    int operator()(int a, double b) { return 10; }
    int operator()(double a, int b) { return 20; }
};

int main() {
    Ambiguous f;
    return f(1, 2); // ambiguous: should fail to compile
}
