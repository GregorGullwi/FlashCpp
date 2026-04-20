// Regression test: concrete static member function bodies from class-template
// instantiations must satisfy the post-parse sema boundary check after pack
// expansion substitution.

struct Accumulator {
	static int sum3(int a, int b, int c) {
		return a + b + c;
	}
};

template <typename... Args>
struct Wrapper {
	static int call(Args... args) {
		return Accumulator::sum3(args...);
	}
};

int main() {
	return Wrapper<int, int, int>::call(10, 20, 12) == 42 ? 0 : 1;
}
