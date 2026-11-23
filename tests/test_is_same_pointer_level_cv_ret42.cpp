// Per-level pointer cv is part of type identity. `int* const*` is a pointer to
// a const pointer, so it is not `int**`, while `int* const*` and
// `int* const*` are the same type. The return value depends on every answer.
struct S {
	int member;
};

// Member and function pointer operands in an `if` condition hit a separate
// binary-trait parse path, so those identities are pinned in static_assert.
static_assert(!__is_same(int (*const)(int), int (*)(int)), "top-level function pointer cv counts");
static_assert(__is_same(int (*)(int), int (*)(int)), "function pointer identity is stable");

int main() {
	if (__is_same(int* const*, int**)) {
		return 1;
	}
	if (!__is_same(int* const*, int* const*)) {
		return 2;
	}
	if (__is_same(int* const* volatile*, int**)) {
		return 3;
	}
	if (!__is_same(int* const* volatile*, int* const* volatile*)) {
		return 4;
	}
	if (__is_same(const int*, int*)) {
		return 5;
	}
	if (!__is_same(const int* const*, const int* const*)) {
		return 6;
	}
	if (__is_same(double* const*, double*)) {
		return 7;
	}
	if (__is_same(int* const*, long* const*)) {
		return 8;
	}
	if (__is_same(char* const*, char**) || __is_same(short* const*, short**)) {
		return 9;
	}
	return 42;
}
