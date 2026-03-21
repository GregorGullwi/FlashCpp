// Test that negative pointer offset dereference is rejected in constexpr.
// Error: "Negative pointer offset -1 in constant expression"

constexpr int arr[] = {10, 20, 30};
constexpr const int* p = &arr[0];

// Dereference at a negative offset
constexpr int bad_deref = *(p - 1);

int main() {
	return 0;
}
