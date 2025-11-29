// Test: dereference with default constructor

struct S {
    int* p;
    const int* cp;
    
    S() : p(nullptr), cp(nullptr) {}  // Explicit default constructor
};

int main() {
    S s;
    int x = 42;
    s.p = &x;
    s.cp = &x;
    if (*s.p != 42) return 1;
    return 0;
}
