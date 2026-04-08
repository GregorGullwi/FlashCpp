namespace geom {
	struct Math {
		static int add(int a, int b) {
			return a + b;
		}

		static int mul(int a, int b) {
			return a * b;
		}
	};
}

struct Ops {
	static int doubleIt(int x) {
		return x * 2;
	}
};

int main() {
	if (Ops::doubleIt(21) != 42)
		return 1;
	if (geom::Math::add(19, 23) != 42)
		return 2;
	if (geom::Math::mul(6, 7) != 42)
		return 3;
	return 0;
}
