// Pointer-based: const int* -> float* changes the underlying type.
// const_cast may only change cv-qualification; changing int* to float* is ill-formed.
// The static_assert uses == 42 so this test only fails (as expected for a _fail test)
// because the evaluator rejects the type-changing const_cast, NOT because the value
// happens to mismatch.
constexpr int value = 42;

constexpr int bad_const_cast() {
	return *const_cast<float*>(&value);
}

static_assert(bad_const_cast() == 42);

int main() {
	return 0;
}
