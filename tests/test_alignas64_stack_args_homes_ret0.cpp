// Realigned RBP must not relocate incoming stack arguments or Win64 param homes.
// Homes that follow `and rbp` can overwrite saved frame data; stack args live at
// positive pre-and offsets and must keep using the canonical frame top.

typedef char* va_list;
#define va_start(ap, param) __builtin_va_start(ap, param)
#define va_arg(ap, type) __builtin_va_arg(ap, type)
#define va_end(ap) ((void)(ap = 0))

static int use_stack_args(int a, int b, int c, int d, int e, int f) {
	alignas(64) int local = a + f;
	if ((reinterpret_cast<unsigned long long>(&local) % 64) != 0) {
		return 10;
	}
	if (a != 1 || b != 2 || c != 3 || d != 4 || e != 5 || f != 6) {
		return 11;
	}
	if (local != 7) {
		return 12;
	}
	return 0;
}

static int use_variadic_homes(int count, ...) {
	alignas(64) int local = count;
	if ((reinterpret_cast<unsigned long long>(&local) % 64) != 0) {
		return 20;
	}
	va_list args;
	va_start(args, count);
	int sum = 0;
	for (int i = 0; i < count; ++i) {
		sum += va_arg(args, int);
	}
	va_end(args);
	if (local != 3 || sum != 60) {
		return 21;
	}
	return 0;
}

int main() {
	const int stack_status = use_stack_args(1, 2, 3, 4, 5, 6);
	if (stack_status != 0) {
		return stack_status;
	}
	const int variadic_status = use_variadic_homes(3, 10, 20, 30);
	if (variadic_status != 0) {
		return variadic_status;
	}
	return 0;
}
