// Pointer-based: const int* -> float* changes the underlying type
constexpr int value = 42;

constexpr int bad_const_cast() {
	return *const_cast<float*>(&value);
}

static_assert(bad_const_cast() == 0);

int main() {
	return 0;
}
