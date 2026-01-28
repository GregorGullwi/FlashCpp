// Test: two dereferences

struct S {
    int* p;
    const int* cp;
};

int main() {
    S s;
    int x = 42;
    s.p = &x;
    s.cp = &x;
    if (*s.p != 42) return 1;
    if (*s.cp != 42) return 2;
    return 0;
}
