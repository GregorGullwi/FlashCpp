// Test that relational comparison of pointers to different arrays is rejected in constexpr.
// Error: "Relational comparison between pointers to different variables is not allowed in constant expressions"

constexpr int arr1[] = {10, 20, 30};
constexpr int arr2[] = {40, 50, 60};
constexpr const int* p1 = &arr1[0];
constexpr const int* p2 = &arr2[0];

// Relational comparison across different arrays is undefined behavior
constexpr bool bad_cmp = p1 < p2;

int main() {
	return 0;
}
