// Test: Constructor member initializer expressions with implicit conversions
// Validates that sema normalizes member initializer expressions so that
// implicit arithmetic conversions (e.g. short + int) are annotated.
// Devin review: member init expressions were not visited by sema, causing
// Phase 15 InternalError.
struct Adder {
int result;
Adder(short x) : result(x + 1) {}
};

struct Multi {
int a;
int b;
Multi(short x, short y) : a(x * 2), b(y + 10) {}
};

int main() {
Adder add(41);
Multi m(5, 20);
int check = add.result + m.a + m.b;
// 42 + 10 + 30 = 82
return check - 82;
}
