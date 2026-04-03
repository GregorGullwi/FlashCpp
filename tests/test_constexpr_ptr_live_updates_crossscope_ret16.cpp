struct Pair {
	int value;
};

constexpr int readScalar(const int* p) {
	return *p;
}

constexpr int readMember(const Pair* p) {
	return p->value;
}

constexpr int evaluateDirectLiveUpdate() {
	int x = 1;
	int* p = &x;
	x = 2;
	return *p;
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

static_assert(evaluateDirectLiveUpdate() == 2);
static_assert(evaluateLiveUpdates() == 16);

int main() {
	constexpr int result = evaluateLiveUpdates();
	return result;
}
