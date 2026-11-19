// An unused [=] still carries a capture default. A lambda with a capture
// default has no conversion to pointer-to-function even when its generated
// closure has no members.

static int apply(int (*function)(int), int value) {
	return function(value);
}

int main() {
	auto increment = [=](int value) {
		return value + 1;
	};
	return apply(increment, 1);
}
