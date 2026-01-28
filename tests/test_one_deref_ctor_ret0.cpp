// Test: one dereference with constructor

struct S {
    int* p;
    const int* cp;
    
    S() : p(nullptr), cp(nullptr) {}
};

int main() {
    S s;
    int x = 42;
    s.p = &x;
    if (*s.p != 42) return 1;  // Only one dereference
    return 0;
}
