struct ConstThisOverload {
	int value;

	constexpr int pick() { return 1; }
	constexpr int pick() const { return value; }

	constexpr int implicitThis() const {
		return pick();
	}

	constexpr int explicitThis() const {
		return this->pick();
	}
};

constexpr int testImplicitThis() {
	ConstThisOverload value{20};
	return value.implicitThis();
}

constexpr int testExplicitThis() {
	ConstThisOverload value{30};
	return value.explicitThis();
}

static_assert(testImplicitThis() == 20);
static_assert(testExplicitThis() == 30);

int main() {
	if (testImplicitThis() != 20) return 1;
	if (testExplicitThis() != 30) return 2;
	return 0;
}
