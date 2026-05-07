struct Pair {
	int value;
};

template <typename T>
struct PairT {
	T value;
};

constexpr void writeScalar(int* p, int value) {
	*p = value;
}

constexpr void addScalar(int* p, int value) {
	*p += value;
}

constexpr void writeScalar(long long* p, long long value) {
	*p = value;
}

constexpr void addScalar(long long* p, long long value) {
	*p += value;
}

constexpr void writeMember(Pair* p, int value) {
	p->value = value;
}

constexpr void addMember(Pair* p, int value) {
	p->value += value;
}

template <typename T>
constexpr void writeMember(PairT<T>* p, T value) {
	p->value = value;
}

template <typename T>
constexpr void addMember(PairT<T>* p, T value) {
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

constexpr long long evaluatePointerParameterWritesMixedTypes() {
	long long wide = 2;
	writeScalar(&wide, 9);
	addScalar(&wide, 3);

	PairT<unsigned short> box{1};
	writeMember(&box, static_cast<unsigned short>(5));
	addMember(&box, static_cast<unsigned short>(4));

	return wide + box.value;
}

constexpr int evaluatePointerParameterWritesArrayElement() {
	PairT<int> pairs[2]{{1}, {2}};
	writeMember(&pairs[1], 11);
	addMember(&pairs[1], 3);

	return pairs[0].value + pairs[1].value;
}

static_assert(evaluatePointerParameterWrites() == 25);
static_assert(evaluatePointerParameterWritesMixedTypes() == 21);
static_assert(evaluatePointerParameterWritesArrayElement() == 15);

constexpr int pointer_parameter_writes = evaluatePointerParameterWrites();
constexpr long long pointer_parameter_writes_mixed_types = evaluatePointerParameterWritesMixedTypes();
constexpr int pointer_parameter_writes_array_element = evaluatePointerParameterWritesArrayElement();

int main() {
	return pointer_parameter_writes == 25 &&
		pointer_parameter_writes_mixed_types == 21 &&
		pointer_parameter_writes_array_element == 15 ? 0 : 1;
}
