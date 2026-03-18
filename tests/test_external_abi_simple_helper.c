// Minimal helper for test_external_abi_simple.cpp
// Keep this separate from test_external_abi_helper.c so the simple test
// does not pull in extra references to FlashCpp-defined symbols.

extern int printf(const char* format, ...);

double external_mixed_params(int a, double b, int c, double d) {
	printf("external_mixed_params: %d %.1f %d %.1f\n", a, b, c, d);
	return a + b + c + d;
}
