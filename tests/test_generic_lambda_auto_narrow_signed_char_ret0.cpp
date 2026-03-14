// Regression test: generic lambda auto parameters should preserve narrow signed
// integer behavior inside the lambda body instead of falling back to plain int
// semantics too early or too late.

int main() {
	auto is_negative = [](auto x) { return x < 0 ? 1 : 0; };
	auto add_one = [](auto x) { return x + 1; };

	signed char negative_char = -1;
	if (is_negative(negative_char) != 1) {
		return 1;
	}

	signed char minus_two = -2;
	if (add_one(minus_two) != -1) {
		return 2;
	}

	return 0;
}
