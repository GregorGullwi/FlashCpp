// Regression test: instantiated out-of-line template constructors should have
// their initializer surfaces checked by the pre-sema boundary pass even while
// the deferred body still belongs to parser-owned lazy materialization.

struct SumBase {
	int total;

	SumBase(int a, int b, int c) : total(a + b + c) {}
};

template <typename... Args>
struct Wrapper : SumBase {
	Wrapper(Args... args);

	int get() const {
		return total;
	}
};

template <typename... Args>
Wrapper<Args...>::Wrapper(Args... args) : SumBase(args...) {}

int main() {
	Wrapper<int, int, int> value(10, 20, 12);
	return value.get() == 42 ? 0 : 1;
}
