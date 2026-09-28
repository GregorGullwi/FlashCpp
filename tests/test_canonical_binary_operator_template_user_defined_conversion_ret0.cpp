struct BinaryOperatorSource {
	int value;
};

struct BinaryOperatorTarget {
	int value;

	BinaryOperatorTarget(const BinaryOperatorSource& source)
		: value(source.value) {}
};

template<class Right>
int operator+(
	const BinaryOperatorTarget& left,
	const Right& right) {
	return left.value + right.value;
}

template<class Unused = void>
int operator+(
	const BinaryOperatorTarget&,
	const BinaryOperatorTarget&) {
	return -1;
}

int main() {
	BinaryOperatorSource left{20};
	BinaryOperatorSource right{22};
	return left + right == 42 ? 0 : 1;
}
