// Test that out-of-bounds pointer dereference is rejected in constexpr.
// Error: "Pointer dereference at offset 5 out of bounds (size 3)"

constexpr int arr[] = {10, 20, 30};
constexpr const int* p = &arr[0];

// Dereference past the end of the array
constexpr int bad_deref = *(p + 5);

int main() {
	return 0;
}
