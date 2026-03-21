// Test that constexpr pointer arithmetic rejects scalar pointers past one-past.
constexpr int value = 42;
constexpr const int* p = &value;

static_assert((p + 2) != p);

int main() {
	return 0;
}
