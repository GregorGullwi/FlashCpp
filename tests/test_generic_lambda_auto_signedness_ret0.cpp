// Regression test: generic lambda auto parameters must keep integer runtime
// semantics instead of falling through the IrType::Void pointer-like path.

int main() {
	auto is_negative = [](auto x) { return x < 0 ? 1 : 0; };
	auto add_one = [](auto x) { return x + 1; };

	if (is_negative(-1) != 1) {
		return 1;
	}

	if (add_one(-2) != -1) {
		return 2;
	}

	return 0;
}
