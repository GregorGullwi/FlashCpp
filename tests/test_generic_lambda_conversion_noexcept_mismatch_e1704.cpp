// A potentially throwing generic lambda cannot convert to a pointer to a
// noexcept function. The opposite direction remains valid and is covered by
// the positive noexcept conversion test.

static int apply(int (*function)(int) noexcept, int value) {
	return function(value);
}

int main() {
	auto identity = [](auto value) {
		return value;
	};
	return apply(identity, 1);
}
