// The C-style cast target reaches the member-pointer owner parse, so a
// non-class owner must be rejected by the shared owner rule instead of being
// read as a parenthesized expression.
typedef int NotAClass;

int main() {
	auto pointer = (int NotAClass::*)0;
	(void)pointer;
	return 0;
}
