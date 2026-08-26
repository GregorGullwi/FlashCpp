// Test that dereferencing a local-variable pointer with a non-zero arithmetic
// offset is diagnosed as a compile-time error (C++20 [expr.const]).
// Specifically: `int x; int* p = &x + 1; *p = 5;` must not be allowed because
// `x` is a scalar (not an array), so any non-zero offset is out-of-bounds.

constexpr int bad_ptr_arith() {
	int x = 5;
	int* p = &x + 1; // non-zero offset on a scalar local
	*p = 99;
	return x;
}
static_assert(bad_ptr_arith() == 99);

int main() { return 0; }
