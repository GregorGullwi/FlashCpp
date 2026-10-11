// Test that missing return in non-void function is handled
// Fallthrough is UB on execution; the structured warning does not reject this definition.
int compute(int x) {
	if (x > 0) {
		return x * 2;
	}
	// Missing return on this path - compiler should warn
}

int main() {
	return compute(21);
}
