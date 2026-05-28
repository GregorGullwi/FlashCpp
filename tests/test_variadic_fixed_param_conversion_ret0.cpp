extern "C" long variadic_take_long(long first, ...);

extern "C" long variadic_take_long(long first, ...) {
	return first;
}

int main() {
	int value = 7;
	long result = variadic_take_long(value, 11, 13);
	return result == 7 ? 0 : 1;
}
