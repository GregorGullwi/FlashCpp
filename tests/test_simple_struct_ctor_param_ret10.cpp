struct A {
    int a = 10;
};

struct B {
    A a;
    B(A a_) : a(a_) {}  // No default argument for now
};

int main() {
    A temp;           // Create A with default initializer
    B b(temp);        // Explicitly pass it to B
    return b.a.a;     // Should return 10
}
