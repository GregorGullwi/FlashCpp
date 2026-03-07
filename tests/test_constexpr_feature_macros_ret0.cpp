// Test constexpr-related feature-test macros only advertise implemented support levels.
int main() {
	constexpr int expected_checks = 3;
	int passed_checks = 0;

	#ifdef __cpp_consteval
	return 10;
	#else
	passed_checks += 1;
	#endif

	#ifndef __cpp_constexpr
	return 11;
	#else
	if (__cpp_constexpr != 201603L) {
		return 12;
	}
	passed_checks += 1;
	#endif

	#ifdef __cpp_constexpr_dynamic_alloc
	return 13;
	#else
	passed_checks += 1;
	#endif

	return passed_checks == expected_checks ? 0 : passed_checks;
}
