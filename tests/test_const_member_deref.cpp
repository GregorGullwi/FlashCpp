// Test: dereference const pointer member

struct S {
    const int* p;
};

int main() {
    S s;
    int x = 42;
    s.p = &x;
    return *s.p;  // Dereference const pointer member
}
