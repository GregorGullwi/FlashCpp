// Regression test: inherited operator() should resolve sema-first through base classes.

struct BaseFunctor {
	int base;

	int operator()(int x) {
		return base + x;
	}

	int operator()(int x, int y) {
		return base + x + y;
	}
};

struct DerivedFunctor : BaseFunctor {
};

int main() {
	DerivedFunctor f;
	f.base = 10;

	int a = f(5);
	int b = f(3, 4);
	return (a == 15 && b == 17) ? 0 : 1;
}
