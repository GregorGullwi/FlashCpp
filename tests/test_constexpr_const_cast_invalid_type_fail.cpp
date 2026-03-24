constexpr int value = 42;

// Pointer-based: const int* -> float* changes the underlying type
constexpr int bad_const_cast_ptr() {
	return *const_cast<float*>(&value);
}

// Reference-based: const int& -> long& changes the underlying type
constexpr long bad_const_cast_ref() {
	return const_cast<long&>(static_cast<const long&>(value));
}

static_assert(bad_const_cast_ptr() == 0);
static_assert(bad_const_cast_ref() == 0);

int main() {
	return 0;
}
