struct Pair {
	int first;
	int second;
};

constexpr int sumWindow(const int* p) {
	return p[0] + p[1] + p[2];
}

constexpr int readSecond(const Pair* p) {
	return p->second;
}

constexpr int evaluatePointerSnapshots() {
	int values[3] = {1, 2, 3};
	const int* begin = &values[0];
	values[1] = 5;
	values[2] = 8;

	Pair pair{4, 6};
	const Pair* pair_ptr = &pair;
	pair.second = 9;

	return sumWindow(begin) + readSecond(pair_ptr);
}

static_assert(evaluatePointerSnapshots() == 23);

int main() {
	return evaluatePointerSnapshots() == 23 ? 0 : 1;
}
