// Test __asm and __asm__ symbol renaming directives
// These are GCC extensions to specify linker symbol names
// FlashCpp strips these via preprocessor macros

extern int foo() __asm("bar");
extern int baz() __asm__("qux");

// More complex example from glibc headers
extern char *strchr(const char *s, int c) __asm("strchr");

int main() {
	// We're just testing that __asm directives are properly stripped
	return 0;
}
