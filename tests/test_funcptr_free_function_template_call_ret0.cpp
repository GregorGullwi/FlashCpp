int square(int x) {
	return x * x;
}

int increment(int x) {
	return x + 1;
}

template <typename F>
int apply(F fn, int x) {
	return fn(x);
}

int main() {
	int deduced_square = apply(square, 5);
	int explicit_square = apply<int (*)(int)>(square, 5);
	int explicit_increment = apply<int (*)(int)>(increment, 41);
	return (deduced_square == 25 && explicit_square == 25 && explicit_increment == 42) ? 0 : 1;
}
