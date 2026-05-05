namespace math {
int value = 10;

int add(int a, int b) {
	return a + b;
}
}

enum Color { Red = 10, Green = 20, Blue = 12 };

namespace config {
Color selected = Blue;
}

enum class Numbers {
	FortyTwo = 42,
};

template <typename T>
struct Counter {
	static int value;
};

template <typename T>
int Counter<T>::value = static_cast<int>(sizeof(T)) + 38;

template <typename T>
struct Ops {
	static int triple(int value) {
		return value * 3;
	}
};

template <typename T>
struct Runner {
	int run(T value) {
		return math::add(Ops<T>::triple(static_cast<int>(value)), 12);
	}
};

int main() {
	if (math::value + 32 != 42)
		return 1;
	if (config::selected + 30 != 42)
		return 2;
	if (static_cast<int>(Numbers::FortyTwo) != 42)
		return 3;
	if (Counter<int>::value != 42)
		return 4;

	Runner<int> runner;
	if (runner.run(10) != 42)
		return 5;

	return 42;
}
