int test(long long value) {
	return value == 42LL ? 42 : 1;
}

int no_parameters() {
	return 0;
}

int one_parameter(int value) {
	return value;
}

int multiple_parameters(int value, double other) {
	return value + static_cast<int>(other);
}

int variadic_parameters(int value, ...) {
	return value;
}
