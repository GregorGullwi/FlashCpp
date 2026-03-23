// Test: empty-brace init on a struct with a user-defined constructor but no
// default constructor must be rejected in constexpr evaluation.
// Per C++20, a type with user-defined constructors is not an aggregate, so
// `NoDflt n{}` is ill-formed (no matching 0-arg constructor).
// This file is a _fail test — compilation must fail.

struct NoDflt {
    int v;
    constexpr NoDflt(int a, int b) : v(a + b) {}
};

constexpr int make() {
    NoDflt n{};   // ERROR: no default constructor
    return n.v;
}

// The static_assert forces constexpr evaluation of make(), which should
// trigger the "No matching constructor" diagnostic.
static_assert(make() == 0);

int main() { return 0; }
