struct Base {
	static constexpr int value = 4;
};

struct Mid : Base {
	static constexpr int value = Base::value + 3;
};

struct Leaf : Mid {
	static constexpr int value = Mid::value + 6;
};

int main() {
	Leaf leaf;
	return Leaf::value + leaf.value - 13;
}
