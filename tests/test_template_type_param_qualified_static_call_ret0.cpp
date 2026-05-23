// Regression test: P::method qualified call where P is a template type parameter
// Bug: UsePolicy<ScaleByTwo>::process called P::scale(x) but codegen couldn't
// resolve the qualified call because 'P' is a type parameter, not a base class.

struct ScaleByTwo {
	static int scale(int x) { return x * 2; }
};

struct ScaleByThree {
	static int scale(int x) { return x * 3; }
};

struct ScaleByTen {
	static int scale(int x) { return x * 10; }
};

template<typename Policy>
struct UsePolicy {
	int process(int x) {
		return Policy::scale(x);
	}
};

// Ensure it works with different sizes/types
template<typename Policy>
struct UseUnsigned {
	unsigned apply(unsigned x) {
		return static_cast<unsigned>(Policy::scale(static_cast<int>(x)));
	}
};

int main() {
	UsePolicy<ScaleByTwo> u2;
	if (u2.process(5) != 10) return 1;

	UsePolicy<ScaleByThree> u3;
	if (u3.process(4) != 12) return 2;

	UsePolicy<ScaleByTen> u10;
	if (u10.process(3) != 30) return 3;

	UseUnsigned<ScaleByTwo> uu;
	if (uu.apply(7u) != 14u) return 4;

	return 0;
}
