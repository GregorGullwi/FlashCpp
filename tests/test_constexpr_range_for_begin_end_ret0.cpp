struct ConstRange {
	int values[4];

	constexpr const int* begin() const { return &values[0]; }
	constexpr const int* end() const { return &values[4]; }
};

struct Holder {
	ConstRange range;
};

constexpr int sum_local_object() {
	ConstRange range{{1, 2, 3, 4}};
	int sum = 0;
	for (int value : range) {
		sum += value;
	}
	return sum;
}

constexpr int sum_member_range() {
	Holder holder{{{3, 1, 4, 1}}};
	int sum = 0;
	for (int value : holder.range) {
		sum += value;
	}
	return sum;
}

constexpr ConstRange make_range() {
	return {{2, 7, 1, 8}};
}

constexpr int sum_returned_range() {
	int sum = 0;
	for (int value : make_range()) {
		sum += value;
	}
	return sum;
}

static_assert(sum_local_object() == 10);
static_assert(sum_member_range() == 9);
static_assert(sum_returned_range() == 18);

int main() {
	return 0;
}
