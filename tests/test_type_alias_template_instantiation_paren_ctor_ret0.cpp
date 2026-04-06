template <typename T>
struct PairBox {
	T left;
	T right;

	PairBox(T lhs, T rhs)
		: left(lhs), right(rhs) {
	}

	int size() const {
		return static_cast<int>(left + right);
	}
};

using IntPairBox = PairBox<int>;

int main() {
	IntPairBox first(19, 23);
	IntPairBox second(20, 22);
	return (first.size() == 42 && second.size() == 42) ? 0 : 1;
}
