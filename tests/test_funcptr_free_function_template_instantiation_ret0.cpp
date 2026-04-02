int square(int x) {
	return x * x;
}

int double_it(int x) {
	return x * 2;
}

template <typename F>
int apply(F fn, int x) {
	return fn(x);
}

template <typename F>
F identity(F fn) {
	return fn;
}

int main() {
	int deduced = apply(square, 3);
	int explicit_param = apply<int (*)(int)>(double_it, 5);
	int (*rebound)(int) = identity<int (*)(int)>(square);
	return (deduced == 9 && explicit_param == 10 && rebound(4) == 16) ? 0 : 1;
}
