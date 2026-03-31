// Test that pointers to local constexpr variables can be dereferenced
// when passed to other constexpr functions (cross-scope dereference).

constexpr int deref(const int* p) {
	return *p;
}

constexpr int outer_simple() {
	constexpr int local = 42;
	return deref(&local);
}
static_assert(outer_simple() == 42);

// Multiple levels of indirection
constexpr int relay(const int* p) {
	return deref(p);
}

constexpr int outer_relay() {
	constexpr int local = 99;
	return relay(&local);
}
static_assert(outer_relay() == 99);

// Pointer passed and used in conditional
constexpr int conditional_deref(const int* p, bool flag) {
	if (flag)
		return *p;
	return 0;
}

constexpr int outer_conditional() {
	constexpr int local = 7;
	return conditional_deref(&local, true);
}
static_assert(outer_conditional() == 7);

int main() {
	return 0;
}
