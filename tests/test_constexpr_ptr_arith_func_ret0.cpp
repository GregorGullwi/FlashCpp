// Test constexpr pointer arithmetic inside constexpr functions
constexpr int sum_via_ptr(const int* p, int n) {
	int total = 0;
	for (int i = 0; i < n; i++) {
		total += *(p + i);
	}
	return total;
}

constexpr int arr[] = {1, 2, 3, 4, 5};
static_assert(sum_via_ptr(&arr[0], 5) == 15);
static_assert(sum_via_ptr(&arr[2], 3) == 12);

constexpr int subscript_sum(const int* p, int n) {
	int total = 0;
	for (int i = 0; i < n; i++) {
		total += p[i];
	}
	return total;
}

static_assert(subscript_sum(&arr[0], 5) == 15);
static_assert(subscript_sum(&arr[1], 4) == 14);

int main() {
	return 0;
}
