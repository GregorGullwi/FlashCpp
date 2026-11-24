// C++20 [conv.qual]/3 adds const to every level between a newly cv-qualified
// inner level and the outermost pointer. Adding const only to the pointee, as
// in "int**" to "const int**", leaves the qualifying outer level without const
// and is therefore not an implicit qualification conversion; [expr.static.cast]/5
// rejects the cast.

int main() {
	int value = 11;
	int* pointer = &value;
	int** plain = &pointer;
	const int** qualified = static_cast<const int**>(plain);
	(void)qualified;
	return 0;
}
