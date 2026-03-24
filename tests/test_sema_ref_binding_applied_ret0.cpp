// Test: member function calls with reference parameters.
// Covers the sema_ref_binding_applied fix: when sema has a valid ref-binding
// annotation but tryApplySemaCallArgReferenceBinding returns nullopt, the legacy
// reference-handling paths must remain active so that ref_qualifier is set on
// the pushed argument. Without the fix the guards check is_valid() and skip the
// legacy reference path, causing pass-by-value without ref_qualifier.
//
// Key invariant: mutations made through a non-const reference parameter must be
// visible to the caller after the call returns. If the argument is erroneously
// passed by value (missing ref_qualifier), the mutation is lost and the test fails.

struct Modifier {
	int stored;

	// Takes lvalue reference - modifies in place
	void increment(int& x) { x = x + 1; }
	void decrement(int& x) { x = x - 1; }

	// Takes const lvalue reference - stores value
	void store(const int& x) { stored = x; }

	// Takes rvalue reference - moves value out
	void take(int&& x) { stored = x; x = 0; }

	// Takes two non-const lvalue references and swaps them
	void swap(int& lhs, int& rhs) {
		int tmp = lhs;
		lhs = rhs;
		rhs = tmp;
	}
};

int main() {
	Modifier m{0};

	// Non-const lvalue reference: mutation must propagate back to caller.
	int a = 10;
	m.increment(a);        // a = 11
	if (a != 11) return 1;

	m.decrement(a);        // a = 10
	if (a != 10) return 2;

	// Const lvalue reference: verify value is read correctly.
	m.store(a);            // stored = 10
	if (m.stored != 10) return 3;

	// Pass an explicit lvalue reference variable to a const-ref param.
	int b = 7;
	int& rb = b;
	m.store(rb);           // stored = 7
	if (m.stored != 7) return 4;

	// Rvalue reference: both mutation and zero-out must propagate.
	m.take(static_cast<int&&>(a));  // stored = 10, a = 0
	if (m.stored != 10) return 5;
	if (a != 0) return 6;

	// Two non-const lvalue references: both must be bound by reference.
	int p = 3, q = 9;
	m.swap(p, q);          // p = 9, q = 3
	if (p != 9) return 7;
	if (q != 3) return 8;

	// Pass a reference variable to a non-const reference parameter.
	// If the argument is passed by value the mutation of c is lost.
	int c = 20;
	int& rc = c;
	m.increment(rc);       // c = rc = 21
	if (c != 21) return 9;
	if (rc != 21) return 10;

	return 0;
}
