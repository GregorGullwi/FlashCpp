// A captureless unary generic lambda establishes its arity from the original
// parameter list before conversion selection. Only the unary function-pointer
// overload is viable; the binary overload must not participate in ranking.

static int invoke(int (*function)(int), int value) {
	return function(value);
}

static int invoke(int (*function)(int, int), int left, int right) {
	return function(left, right);
}

int main() {
	auto increment = [](auto value) {
		return value + 1;
	};
	if (invoke(increment, 41) != 42) {
		return 1;
	}
	return 0;
}
