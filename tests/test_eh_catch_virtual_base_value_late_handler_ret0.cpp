// Regression test: a later typed catch handler must materialize a virtual-base
// catch object by value with the correct size, even after earlier handlers do
// not match.

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
	bool caught = false;
	try {
		throw Derived{};
	} catch (int) {
		return 1;
	} catch (VBase v) {
		if (v.vb != 17)
			return 2;
		caught = true;
	}
	return caught ? 0 : 3;
}