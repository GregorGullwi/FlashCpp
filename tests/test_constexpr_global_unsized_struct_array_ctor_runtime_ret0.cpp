struct Pair {
	int first;
	int second;

	constexpr Pair(int x, int y) : first(y), second(x + 1) {}
};

constexpr Pair arr[] = {{1, 2}, {3, 4}, {5, 6}};

static_assert(sizeof(arr) / sizeof(arr[0]) == 3);
static_assert(arr[0].first == 2 && arr[0].second == 2);
static_assert(arr[1].first == 4 && arr[1].second == 4);
static_assert(arr[2].first == 6 && arr[2].second == 6);

int main() {
	const Pair* p = arr;
	int total = p[0].first + p[0].second + p[1].first + p[1].second;
	return (sizeof(arr) / sizeof(arr[0]) == 3 && total == 12)
		? 0
		: 1;
}
