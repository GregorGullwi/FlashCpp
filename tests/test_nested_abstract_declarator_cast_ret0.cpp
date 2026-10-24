// C++20 [dcl.decl]: an abstract-declarator may be a parenthesized
// ptr-abstract-declarator, and that group may itself be parenthesized.
// int ((*)()) is the same pointer-to-function type as int (*)().

int pick() {
	return 7;
}

int add(int value) {
	return value + 1;
}

int main() {
	auto direct = static_cast<int (*)()>(pick);
	auto nested = static_cast<int ((*)())>(pick);
	int (*c_style)() = (int ((*)()))pick;
	auto with_param = static_cast<int ((*)(int))>(add);
	int row[4] = {1, 2, 3, 4};
	int (*array_ptr)[4] = static_cast<int ((*)[4])>(&row);

	if (direct() != 7) {
		return 1;
	}
	if (nested() != 7) {
		return 2;
	}
	if (c_style() != 7) {
		return 3;
	}
	if (with_param(6) != 7) {
		return 4;
	}
	if ((*array_ptr)[0] != 1) {
		return 5;
	}
	if (sizeof(int ((*)())) != sizeof(int (*)())) {
		return 6;
	}
	return 0;
}
