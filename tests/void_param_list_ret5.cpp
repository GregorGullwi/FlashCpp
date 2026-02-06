// Test: C-style (void) parameter list meaning "no parameters"
extern int foo(void);

int foo(void) {
	return 5;
}

int main() {
	return foo();
}
