// Test that constexpr pointer arithmetic also rejects invalid pointer formation
// for local arrays evaluated through bindings-aware constexpr execution.

constexpr bool bad_local_pointer() {
	constexpr int arr[] = {10, 20, 30};
	constexpr const int* begin = &arr[0];
	return (begin - 1) != begin;
}

static_assert(bad_local_pointer());

int main() {
	return 0;
}
