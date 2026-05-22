enum Tiny : unsigned char {
	One = 1,
	Two = 2,
	Big = 250,
};

template <typename T>
struct Box {
	T value;

	int addInt(int rhs) const {
		return value + rhs;
	}

	bool greaterThan(T rhs) const {
		return value > rhs;
	}
};

int main() {
	Box<Tiny> box{Big};
	if (box.addInt(1) != 251)
		return 1;
	if (!box.greaterThan(Two))
		return 2;

	Box<Tiny> low{One};
	if (low.addInt(2) != 3)
		return 3;

	return 0;
}
