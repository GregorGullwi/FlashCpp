// if constexpr decisions belong to the selected call-operator specialization,
// not to the lambda body shared by every conversion probe. Converting the same
// generic lambda to int (*)(int) and int (*)(double) must produce the branch
// selected for each deduced parameter type.

static int apply_int(int (*function)(int), int value) {
	return function(value);
}

static int apply_double(int (*function)(double), double value) {
	return function(value);
}

int main() {
	auto pick = [](auto value) {
		if constexpr (sizeof(value) == 4) {
			return 11;
		} else {
			return 22;
		}
	};
	if (apply_int(pick, 1) != 11) {
		return 1;
	}
	if (apply_double(pick, 1.0) != 22) {
		return 2;
	}
	return 0;
}
