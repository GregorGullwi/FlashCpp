// Regression test: operator* defined at the grandparent level,
// invoked on a grandchild via *obj.
struct GrandBase {
	int* ptr;
	int& operator*() { return *ptr; }
};

struct Middle : GrandBase {
	Middle& operator++() {
		++ptr;
		return *this;
	}
};

struct Leaf : Middle {};

int main() {
	int value = 99;
	Leaf it;
	it.ptr = &value;
	int x = *it;
	return x - 99;
}
