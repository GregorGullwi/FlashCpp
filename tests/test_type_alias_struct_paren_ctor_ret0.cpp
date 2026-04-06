struct Pair {
	int a;
	int b;

	Pair(int left, int right)
		: a(left), b(right) {
	}

	int sum() const {
		return a + b;
	}
};

using PairAlias = Pair;

int makeAliasSum() {
	return PairAlias(19, 23).sum();
}

int main() {
	PairAlias value(20, 22);
	return (value.sum() == 42 && makeAliasSum() == 42) ? 0 : 1;
}
