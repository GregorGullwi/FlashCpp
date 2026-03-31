// Test range-based for loop in constexpr functions

// Range-based for over local int array
constexpr int sum_arr() {
	int arr[] = {1, 2, 3, 4, 5};
	int sum = 0;
	for (int x : arr) {
		sum += x;
	}
	return sum;
}

static_assert(sum_arr() == 15);

// Range-based for with break
constexpr int first_gt(int n) {
	int arr[] = {1, 4, 9, 16, 25};
	for (int x : arr) {
		if (x > n)
			return x;
	}
	return -1;
}

static_assert(first_gt(5) == 9);
static_assert(first_gt(20) == 25);

// Range-based for over struct array
struct Pair {
	int key;
	int value;
};

constexpr int find_value(int key) {
	Pair pairs[] = {{1, 10}, {2, 20}, {3, 30}};
	for (auto p : pairs) {
		if (p.key == key)
			return p.value;
	}
	return -1;
}

static_assert(find_value(1) == 10);
static_assert(find_value(2) == 20);
static_assert(find_value(3) == 30);
static_assert(find_value(4) == -1);

int main() { return 0; }
