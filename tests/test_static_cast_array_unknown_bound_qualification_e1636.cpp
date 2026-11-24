// [conv.qual]/3.2 makes the qualification-combined type keep an unknown array
// bound whenever either side has one. "int (*)[]" can therefore combine into
// "int (*)[]" (or add cv to it), but never into the known bound "int (*)[4]",
// so [expr.static.cast]/5 rejects the known-bound target.

int main() {
	int row[4] = {1, 2, 3, 4};
	int (*unknown)[] = &row;
	int (*known)[4] = static_cast<int (*)[4]>(unknown);
	(void)known;
	return 0;
}
