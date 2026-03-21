// Test that a constexpr pointer to a scalar object may form a one-past pointer.
constexpr int value = 42;
constexpr const int* p = &value;
constexpr const int* end = p + 1;

static_assert(end - p == 1);
static_assert(end != p);

int main() {
	return 0;
}
