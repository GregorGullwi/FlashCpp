// Diagnostic regression for operators that must be non-static members.
struct Value {};

int operator()(const Value&, int) {
	return 0;
}

int main() {
	return 0;
}
