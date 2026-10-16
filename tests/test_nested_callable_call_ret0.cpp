// A call whose receiver is itself a call's function-pointer result must be typed
// from the inner callable signature carried on the outer function's structured
// return type, so the second call resolves instead of failing closed with
// "callable operator() receiver type could not be resolved".
int add_two(int x) { return x + 2; }
int square(int x) { return x * x; }
int (*select_adder(int which))(int) { (void)which; return &add_two; }
int (*select_square(int which))(int) { (void)which; return &square; }

int main() {
	int (* (*local_adder)(int))(int) = &select_adder;
	int (* (*local_square)(int))(int) = &select_square;
	const bool add_ok = local_adder(0)(40) == 42;
	const bool square_ok = local_square(1)(7) == 49;
	return add_ok && square_ok ? 0 : 1;
}
