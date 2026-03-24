// Reference-based: const int& -> long& changes the underlying type,
// which is not a valid const_cast (only cv/ref changes are allowed).
// The static_assert uses == 42 so this test only fails (as expected for a _fail test)
// because the evaluator rejects the type-changing const_cast, NOT because the value
// happens to mismatch.
constexpr int value = 42;

constexpr long bad_const_cast_ref() {
	return const_cast<long&>(value);
}

static_assert(bad_const_cast_ref() == 42);

int main() {
	return 0;
}
