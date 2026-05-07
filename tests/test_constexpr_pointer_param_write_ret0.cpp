struct Pair {
	int value;
};

constexpr void writeScalar(int* p, int value) {
	*p = value;
}

constexpr void addScalar(int* p, int value) {
	*p += value;
}

constexpr void writeMember(Pair* p, int value) {
	p->value = value;
}

constexpr void addMember(Pair* p, int value) {
	p->value += value;
}

constexpr int evaluatePointerParameterWrites() {
	int x = 1;
	writeScalar(&x, 7);
	addScalar(&x, 5);

	Pair pair{3};
	writeMember(&pair, 11);
	addMember(&pair, 2);

	return x + pair.value;
}

static_assert(evaluatePointerParameterWrites() == 25);

int main() {
	return evaluatePointerParameterWrites() == 25 ? 0 : 1;
}
