// Test constexpr-related feature-test macros only advertise implemented support levels.
int main() {
	int result = 0;

	#ifndef __cpp_consteval
	result += 1;
	#endif

	#ifdef __cpp_constexpr
	if (__cpp_constexpr == 201603L) {
		result += 1;
	}
	#endif

	#ifndef __cpp_constexpr_dynamic_alloc
	result += 1;
	#endif

	return result == 3 ? 0 : result;
}
