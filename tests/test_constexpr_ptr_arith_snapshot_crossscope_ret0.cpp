// Regression test: pointer arithmetic (ptr + n) must propagate the snapshot
// from the source pointer so that the result can be dereferenced after the
// original array has gone out of scope.
//
// Currently fails because make_checked_constexpr_pointer_result creates a
// fresh EvalResult via from_pointer() with an empty pointer_value_snapshot.
// When readOne() tries to dereference *p, the target array "arr" is no longer
// in the callee's bindings and the empty snapshot causes a fallback to the
// symbol table, which fails for local variables.

constexpr int readOne(const int* p) {
	return *p;
}

constexpr int readViaArith() {
	int arr[3] = {10, 20, 30};
	const int* p = &arr[0];
	const int* q = p + 2;      // q should carry p's snapshot
	return readOne(q);          // cross-scope: arr not in callee bindings
}

static_assert(readViaArith() == 30);

int main() {
	return readViaArith() == 30 ? 0 : 1;
}
