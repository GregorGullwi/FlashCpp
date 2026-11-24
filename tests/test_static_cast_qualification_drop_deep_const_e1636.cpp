// A static_cast cannot strip cv-qualification at any pointer level. Casting
// "const int* const*" to "int**" drops const from both the inner pointer and
// the pointee, which [conv.qual]/3 forbids and [expr.static.cast]/5 therefore
// rejects.

int main() {
	int value = 13;
	const int* pointer = &value;
	const int* const* qualified = &pointer;
	int** plain = static_cast<int**>(qualified);
	(void)plain;
	return 0;
}
