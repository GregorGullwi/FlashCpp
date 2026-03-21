// Test that forming a pointer before the beginning of a constexpr array is rejected.

constexpr int arr[] = {10, 20, 30};
constexpr const int* begin = &arr[0];
static_assert((begin - 1) != begin);

int main() {
	return 0;
}
