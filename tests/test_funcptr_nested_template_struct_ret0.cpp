typedef int (*IntFn)(int);

template <typename F>
struct Outer {
	struct Inner {
		F fn;
	};

	Inner inner;

	int call(int value) const {
		return inner.fn(value);
	}
};

int increment(int value) {
	return value + 1;
}

int main() {
	Outer<IntFn> outer{{increment}};
	return outer.call(41) == 42 ? 0 : 1;
}
