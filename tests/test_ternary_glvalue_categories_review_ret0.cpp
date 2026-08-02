int select_category(int&) {
	return 10;
}

int select_category(int&&) {
	return 20;
}

int category_left = 3;
int category_right = 4;

int& select_lvalue_reference(bool choose_left) {
	return choose_left ? category_left : category_right;
}

int&& select_xvalue_reference(bool choose_left) {
	return choose_left
		? static_cast<int&&>(category_left)
		: static_cast<int&&>(category_right);
}

int main() {
	int left = 1;
	int right = 2;
	if (select_category(true ? left : right) != 10) {
		return 1;
	}
	(true ? left : right) = 7;
	if (left != 7) {
		return 2;
	}

	int* left_pointer = &left;
	int* right_pointer = &right;
	(true ? left_pointer : right_pointer) = &right;
	if (left_pointer != &right) {
		return 3;
	}

	if (select_category(true
		? static_cast<int&&>(left)
		: static_cast<int&&>(right)) != 20) {
		return 4;
	}
	int&& selected = true
		? static_cast<int&&>(left)
		: static_cast<int&&>(right);
	selected = 9;
	if (left != 9) {
		return 5;
	}
	if (select_category(true
		? left
		: static_cast<int&&>(right)) != 20) {
		return 6;
	}
	if (select_category(true
		? select_lvalue_reference(true)
		: select_lvalue_reference(false)) != 10) {
		return 7;
	}
	if (select_category(true
		? select_xvalue_reference(true)
		: select_xvalue_reference(false)) != 20) {
		return 8;
	}

	return 0;
}
