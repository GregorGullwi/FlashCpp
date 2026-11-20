// A namespace-scope initializer that names a using-declared variable resolves
// the variable's own initializer in the declaring namespace, not in the
// importing namespace where a shadowing name exists.

namespace A {
constexpr int seed = 5;
constexpr int base = seed + 1;
}

namespace B {
using A::base;
constexpr int seed = 50;
constexpr int stored = base;
}

static_assert(B::stored == 6, "using-declared initializer uses its declaring namespace");

int main() {
	return B::stored;
}
