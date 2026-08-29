// Test that an evaluation failure inside a ternary struct initializer is
// propagated as the real error rather than being silenced and replaced with the
// generic "Constexpr member access requires a struct initializer" message.
// This is a negative (fail) test — the compiler must reject it.

struct Pt { int x; int y; };
constexpr int undef_fn();   // declared but not defined — causes evaluation failure

// Accessing a member of a constexpr struct variable whose initializer is a
// ternary containing an unevaluable call must surface the real error.
constexpr Pt p = (true ? Pt{undef_fn(), 2} : Pt{1, 2});
constexpr int v = p.x;
static_assert(v == 1);

int main() { return 0; }
