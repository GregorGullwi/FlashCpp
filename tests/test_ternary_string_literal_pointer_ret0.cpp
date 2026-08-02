// C++20 [expr.cond]: an array operand is converted to a pointer when the
// other conditional operand is a compatible pointer. String literals retain
// their array type until that conversion is selected.
const char* choose_message(const char* message, bool use_message) {
	return use_message ? message : "fallback";
}

const int left_values[2] = {1, 2};
const int right_values[2] = {3, 4};

const int* choose_int_array(bool choose_left) {
	return choose_left ? left_values : right_values;
}

int* choose_local_array(int* selected, bool use_selected) {
	int fallback[2] = {5, 6};
	return use_selected ? selected : fallback;
}

int* choose_nullptr(int* selected, bool use_selected) {
	return use_selected ? selected : nullptr;
}

int* choose_zero(int* selected, bool use_selected) {
	return use_selected ? selected : 0;
}

int main() {
	const char* message = "message";
	if (choose_message(message, true)[0] != 'm') {
		return 1;
	}
	if (choose_message(message, false)[0] != 'f') {
		return 2;
	}
	if (choose_int_array(false)[0] != 3) {
		return 3;
	}
	if (choose_local_array(nullptr, true) != nullptr) {
		return 4;
	}
	int selected = 8;
	if (choose_nullptr(&selected, false) != nullptr) {
		return 5;
	}
	if (choose_zero(&selected, false) != nullptr) {
		return 6;
	}
	return 0;
}
