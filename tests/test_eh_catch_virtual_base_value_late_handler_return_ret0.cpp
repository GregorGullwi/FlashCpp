// Regression test: a later by-value virtual-base catch that returns directly
// must not have its catch object clobbered by FH3 catch-return spill slots.

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
	try {
		throw Derived{};
	} catch (int) {
		return 1;
	} catch (VBase v) {
		return v.vb == 17 ? 0 : 2;
	}
	return 3;
}