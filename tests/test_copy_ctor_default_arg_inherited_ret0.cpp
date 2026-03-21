// Test: copy ctor with default args is properly handled across different paths.
// Verifies the min-required-args relaxation in ad-hoc detection sites.

struct Base {
    int x;
    Base(int v) : x(v) {}
    Base(const Base& other, int extra = 0) : x(other.x + extra) {}
};

int main() {
    Base a(100);
    Base b(a);        // copy ctor with default arg => x == 100
    Base c(a, 5);     // copy ctor with explicit arg => x == 105
    Base d = b;       // copy initialization => x == 100
    return b.x + c.x + d.x - 305;  // 100 + 105 + 100 - 305 == 0
}
