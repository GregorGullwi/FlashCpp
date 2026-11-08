using Callback = int (*)(int);

int increment(int value) {
	return value + 1;
}

int main() {
	int value = 41;
	int* pointer = &value;
	int* first = true ? pointer : pointer;
	if (*first != 41) {
		return 1;
	}
	Callback local = &increment;
	Callback second = true ? local : local;
	if (second(41) != 42) {
		return 2;
	}
	Callback third = false ? local : local;
	if (third(9) != 10) {
		return 3;
	}
	Callback& reference = local;
	Callback fourth = true ? reference : local;
	if (fourth(5) != 6) {
		return 4;
	}
	Callback target = nullptr;
	Callback& destination = target;
	destination = local;
	if (target(7) != 8) {
		return 5;
	}
	int (**pointer_to_callback)(int) = &local;
	int (**selected_pointer)(int) = true ? pointer_to_callback : pointer_to_callback;
	return (*selected_pointer)(7) == 8 ? 0 : 6;
}
