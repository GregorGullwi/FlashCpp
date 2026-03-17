// Nested ternary: verify sema annotations are not overwritten during recursion.
// C++20 [expr.cond]/7: usual arithmetic conversions on both branches of each ternary.
int main() {
    int a = 5;
    double d = 3.5;
    float f = 1.5f;

    // Outer ternary: common type of (int, nested-ternary-result).
    // Inner ternary: common type of (double, float) = double.
    // Outer ternary: common type of (int, double) = double.
    // a < 0 is false, so we take the false branch (inner ternary).
    // a < 5 is false (5 < 5 is false), so inner false branch: f (1.5f) promoted to double.
    // Expected result: 1.5
    double r1 = (a < 0) ? a : ((a < 5) ? d : f);
    if (r1 != 1.5)
        return 1;

    // a < 0 is false, a < 5... wait a=5 so a < 5 is false, inner result is f=1.5 -> double 1.5
    // Let's also test with inner true branch:
    int b = 3;
    // b < 0 is false, so false branch = inner ternary
    // b < 5 is true, so inner true branch = d = 3.5
    double r2 = (b < 0) ? b : ((b < 5) ? d : f);
    if (r2 != 3.5)
        return 2;

    return 0;
}
