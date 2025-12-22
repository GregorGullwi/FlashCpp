// Minimal test: dereference a pointer member

struct S {
    int* p;
};

int main() {
    S s;
    int x = 42;
    s.p = &x;
    return *s.p;  // Dereference pointer member
}
