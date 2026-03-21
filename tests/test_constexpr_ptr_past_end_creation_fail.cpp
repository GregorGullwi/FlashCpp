// Test that forming a pointer past one-past-the-end of a constexpr array is rejected.

constexpr int arr[] = {10, 20, 30};
constexpr const int* begin = &arr[0];
static_assert((begin + 4) != begin);

int main() {
	return 0;
}
