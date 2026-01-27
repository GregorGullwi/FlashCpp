// Test: two dereferences of SAME member

struct S {
    int* p;
    const int* cp;
    
    S() : p(nullptr), cp(nullptr) {}
};

int main() {
    S s;
    int x = 42;
    s.p = &x;
    if (*s.p != 42) return 1;
    if (*s.p != 42) return 2;  // Same member twice
    return 0;
}
