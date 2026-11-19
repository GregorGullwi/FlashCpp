// A non-dependent lambda parameter must match the target function-pointer
// parameter exactly. The fixed int parameter cannot bind the double parameter,
// so the target does not provide a viable conversion.

static int apply(int (*function)(double), double value) {
	return function(value);
}

int main() {
	auto increment = [](int value) {
		return value + 1;
	};
	return apply(increment, 1.0);
}
