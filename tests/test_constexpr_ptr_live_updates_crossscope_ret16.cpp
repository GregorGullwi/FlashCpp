struct Pair {
	int value;
};

constexpr int readScalar(const int* p) {
	return *p;
}

constexpr int readMember(const Pair* p) {
	return p->value;
}

constexpr int evaluateLiveUpdates() {
	int x = 1;
	const int* px = &x;
	x = 2;

	Pair pair{3};
	const Pair* pp = &pair;
	pair.value = 5;

	int y = 4;
	const int* py = &y;
	y = 9;

	return readScalar(px) + readMember(pp) + readScalar(py);
}

static_assert(evaluateLiveUpdates() == 16);

int main() {
	constexpr int result = evaluateLiveUpdates();
	return result;
}
