// Regression test: a by-value catch through a virtual base must not inherit
// stale reference metadata from an earlier reference catch that reused the
// same local slot.

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
	bool caught_ref = false;
	try {
		throw Derived{};
	} catch (VBase& v) {
		if (v.vb != 17) return 1;
		caught_ref = true;
	}
	if (!caught_ref) return 2;

	bool caught_value = false;
	try {
		throw Derived{};
	} catch (VBase v) {
		if (v.vb != 17) return 3;
		caught_value = true;
	}
	return caught_value ? 0 : 4;
}