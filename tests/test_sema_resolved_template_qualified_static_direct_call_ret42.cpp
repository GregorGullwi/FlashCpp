namespace math {
	int add(int a, int b) {
		return a + b;
	}
}

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
	Runner<int> runner;
	return runner.run(10);
}
