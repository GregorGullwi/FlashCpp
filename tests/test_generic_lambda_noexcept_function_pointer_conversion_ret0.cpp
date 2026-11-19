// A noexcept generic lambda can convert to a pointer to a noexcept function
// and still satisfy the target signature after invented parameter deduction.

static int apply(int (*function)(int) noexcept, int value) {
	return function(value);
}

int main() {
	auto identity = [](auto value) noexcept {
		return value;
	};
	return apply(identity, 42) == 42 ? 0 : 1;
}
