// [conv.qual]/3 requires the same pointer components on both sides, so a
// static_cast may not add or remove a pointer level. "int*" to "int**" is not
// an implicit conversion sequence and [expr.static.cast]/5 rejects it.

int main() {
	int value = 19;
	int* pointer = &value;
	int** plain = static_cast<int**>(pointer);
	(void)plain;
	return 0;
}
