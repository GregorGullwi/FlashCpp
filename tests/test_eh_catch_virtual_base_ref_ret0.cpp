// Regression test: thrown objects with virtual inheritance must be catchable
// through the shared virtual base by reference on Windows FH3.

struct VBase {
	virtual ~VBase() {}
	int vb = 17;
};

struct Left : virtual VBase {
	int left = 1;
};

struct Right : virtual VBase {
	int right = 2;
};

struct Derived : Left, Right {
	int derived = 3;
};

int main() {
	bool caught_left = false;
	try {
		throw Derived{};
	} catch (Left&) {
		caught_left = true;
	}
	if (!caught_left) return 1;

	bool caught_vbase = false;
	try {
		throw Derived{};
	} catch (VBase& v) {
		if (v.vb != 17) return 2;
		caught_vbase = true;
	}
	return caught_vbase ? 0 : 3;
}