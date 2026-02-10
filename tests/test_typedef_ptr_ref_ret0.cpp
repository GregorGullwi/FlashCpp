// Test typedef with pointer and reference declarators
// Validates consume_pointer_ref_modifiers() handles cv-qualifiers on pointers in typedef context

typedef int* IntPtr;
typedef const int* ConstIntPtr;
typedef int** IntPtrPtr;
typedef int& IntRef;

int main() {
	int val = 42;
	IntPtr p = &val;
	ConstIntPtr cp = &val;
	IntPtrPtr pp = &p;
	IntRef r = val;

	if (*p != 42) return 1;
	if (*cp != 42) return 2;
	if (**pp != 42) return 3;
	if (r != 42) return 4;
	return 0;
}
