// Test pointer-reference binding: int*& and typedef int*& IntPtrRef.
// A reference to a pointer should bind to the pointer variable itself,
// allowing modification of the pointer through the reference.

typedef int*& IntPtrRef;

int main() {
	int val = 42;
	int* p = &val;

	// Direct int*& binding
	int*& pr = p;
	if (*pr != 42) return 1;

	// Typedef'd int*& binding
	IntPtrRef tpr = p;
	if (*tpr != 42) return 2;

	// Modify through the pointer-reference
	int other = 99;
	pr = &other;
	// p should now point to other (since pr is a reference to p)
	if (*p != 99) return 3;

	return 0;
}
