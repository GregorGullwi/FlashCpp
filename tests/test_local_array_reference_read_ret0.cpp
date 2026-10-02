// A local reference bound to an array must read and write the referenced
// elements, both for a named array and for a string literal. Binding the
// reference must not leave the frame slot holding the array's bytes while the
// read path dereferences it as a pointer.
int sum(const int (&r)[3]) {
	return r[0] + r[1] + r[2];
}

int main() {
	int values[3] = {1, 2, 3};
	int (&ref)[3] = values;
	ref[1] = 20;
	if (values[1] != 20) return 1;
	if (sum(values) != 24) return 2;

	const char (&text)[6] = "hello";
	if (text[0] != 'h' || text[4] != 'o' || text[5] != 0) return 3;

	int copy[3];
	int (&copy_ref)[3] = copy;
	for (int i = 0; i < 3; ++i) {
		copy_ref[i] = values[i] + 1;
	}
	if (copy[0] != 2 || copy[1] != 21 || copy[2] != 4) return 4;
	return 0;
}
