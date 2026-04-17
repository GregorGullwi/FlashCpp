struct Pair {
	int first;
	int second;

	constexpr Pair(int x, int y) : first(y), second(x + 1) {}
};

constexpr Pair arr[2] = {{1, 2}, {3, 4}};

static_assert(arr[0].first == 2 && arr[0].second == 2);
static_assert(arr[1].first == 4 && arr[1].second == 4);

int main() {
	return (arr[0].first == 2 &&
			arr[0].second == 2 &&
			arr[1].first == 4 &&
			arr[1].second == 4)
		? 0
		: 1;
}
