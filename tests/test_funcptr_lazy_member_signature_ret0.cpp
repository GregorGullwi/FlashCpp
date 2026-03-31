template <typename F>
struct Wrapper {
	F func;

	int call(int value);
};

template <typename F>
int Wrapper<F>::call(int value) {
	return func(value);
}

int increment(int value) {
	return value + 1;
}

int main() {
	Wrapper<int (*)(int)> wrapper{increment};
	return wrapper.call(41) == 42 ? 0 : 1;
}
