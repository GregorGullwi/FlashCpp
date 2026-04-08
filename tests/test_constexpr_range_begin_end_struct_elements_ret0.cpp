struct Pair {
	int key;
	int value;
};

struct PairVec {
	Pair data[3] = {{1, 10}, {2, 20}, {3, 30}};
	constexpr Pair* begin() { return data; }
	constexpr Pair* end() { return &data[3]; }
};

constexpr int find_value_by_key() {
	PairVec v{};
	for (auto p : v) {
		if (p.key == 2)
			return p.value;
	}
	return -1;
}

static_assert(find_value_by_key() == 20);

struct BoundedPairVec {
	Pair data[4] = {{4, 40}, {5, 50}, {6, 60}, {7, 70}};
	int size = 3;
	constexpr Pair* begin() { return &data[0]; }
	constexpr Pair* end() { return &data[size]; }
};

constexpr int sum_values_before_end() {
	BoundedPairVec v{};
	int sum = 0;
	for (Pair p : v) {
		sum += p.value;
	}
	return sum;
}

static_assert(sum_values_before_end() == 150);

int main() { return 0; }
