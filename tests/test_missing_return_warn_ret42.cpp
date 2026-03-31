// Test that missing return in non-void function is handled
// The warning is a log message, not a hard error, so this should compile
int compute(int x) {
	if (x > 0) {
		return x * 2;
	}
	// Missing return on this path - compiler should warn
}

int main() {
	return compute(21);
}
