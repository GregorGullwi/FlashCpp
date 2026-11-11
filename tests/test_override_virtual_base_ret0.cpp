struct Base {
	virtual int value() const { return 1; }
};

struct Mid : virtual Base {
	int value() const override { return 2; }
};

struct Leaf : Mid {
	int value() const override { return 3; }
};

int main() {
	Leaf leaf;
	Base* base = &leaf;
	return base->value() == 3 && leaf.value() == 3 ? 0 : 1;
}
