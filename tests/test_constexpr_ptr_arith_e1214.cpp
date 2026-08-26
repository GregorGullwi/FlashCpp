// Test that addition of two pointers is rejected in constexpr.
// Error: "Addition of two pointers is not allowed in constant expressions"

constexpr int arr[] = {10, 20, 30};
constexpr const int* p1 = &arr[0];
constexpr const int* p2 = &arr[1];

// ptr + ptr is ill-formed
constexpr auto bad_add = p1 + p2;

int main() {
	return 0;
}
