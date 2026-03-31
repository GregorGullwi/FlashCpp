// Test exception hierarchy matching across multiple inheritance.
// In particular, catch through a non-primary public base should match and
// adjust the caught reference to the correct base subobject on Windows.

struct Left {
	virtual ~Left() {}
	int left = 11;
};

struct Right {
	virtual ~Right() {}
	int right = 22;
};

struct Derived : public Left, public Right {
	int derived = 33;
};

struct MostDerived : public Derived {
	int most = 44;
};

int main() {
	bool caught = false;
	try {
		throw Derived{};
	} catch (Left& left) {
		if (left.left != 11)
			return 1;
		caught = true;
	}
	if (!caught)
		return 2;

	caught = false;
	try {
		throw Derived{};
	} catch (Right& right) {
		if (right.right != 22)
			return 3;
		caught = true;
	}
	if (!caught)
		return 4;

	caught = false;
	try {
		throw MostDerived{};
	} catch (Right& right) {
		if (right.right != 22)
			return 5;
		caught = true;
	}
	if (!caught)
		return 6;

	return 0;
}