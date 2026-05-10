// Regression test: constexpr expression substitution / constant folding in codegen.
//
// Verifies that constexpr function calls, constexpr global variables, constexpr
// lambda callables, and compound ternary expressions are all folded to a single
// literal constant so the assembly for main consists only of a direct immediate load.

constexpr int add(int a, int b) { return a + b; }

constexpr auto make_mul3() {
    return [](int x) { return x * 3; };
}
constexpr auto mul3 = make_mul3();

constexpr auto scale = [](int x) { return x * 5; };

constexpr int g_x = 7;

int main() {
    // Free constexpr function call — folded
    int r1 = add(3, 4) == 7 ? 0 : 1;
    // Constexpr global variable — folded
    int r2 = g_x == 7 ? 0 : 2;
    // Constexpr lambda returned from a constexpr function — folded
    int r3 = mul3(4) == 12 ? 0 : 3;
    // Direct constexpr lambda variable — folded
    int r4 = scale(3) == 15 ? 0 : 4;
    // Compound: lambda + ternary
    int r5 = mul3(5) == 15 ? 0 : 5;
    return r1 + r2 + r3 + r4 + r5;
}
