// Diagnostic regression for operators that must be non-static members.
struct Value {};

Value* operator->(Value& value) {
	return &value;
}

int main() {
	return 0;
}
