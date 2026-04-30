// Regression: C++20 [expr.add]/4 — integer + pointer must be equivalent to
// pointer + integer (commutative addition).  Before this fix, codegen only
// scaled the offset when the LHS was the pointer; the reversed form fell
// through to raw integer addition without scaling by sizeof(T).
//
// Expected exit code: 0

int main() {
	int arr[4] = {10, 20, 30, 40};
	int* p = arr;

	// Commutative: int + T*
	int* q = 2 + p;
	if (*q != 30)
		return 1;

	// Composed: int + (ptr + int)
	int* r = 1 + (p + 1);
	if (*r != 30)
		return 2;

	return 0;
}
