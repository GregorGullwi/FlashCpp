// Test that *ptr = value works when ptr points to a local variable in the same
// constexpr function frame (C++20 [expr.const]).  Previously this produced:
//   "Dereference assignment: pointer does not refer to a constexpr heap object"
// because the dereference-assignment path only checked the constexpr heap.

constexpr int simple_deref_assign() {
	int x = 0;
	int* p = &x;
	*p = 5;
	return x;
}
static_assert(simple_deref_assign() == 5);

// Compound assignment through local pointer
constexpr int compound_deref_assign() {
	int x = 3;
	int* p = &x;
	*p += 7;
	return x;
}
static_assert(compound_deref_assign() == 10);

// Pointer updated after initial assignment, then written through
constexpr int repointed_deref_assign() {
	int a = 1;
	int b = 2;
	int* p = &a;
	p = &b;
	*p = 99;
	return b;
}
static_assert(repointed_deref_assign() == 99);

// Ensure the original variable is actually updated (not just a copy)
constexpr int deref_assign_visible_through_original() {
	int x = 0;
	int* p = &x;
	*p = 42;
	return x;   // must see the write through p
}
static_assert(deref_assign_visible_through_original() == 42);

// Multiple writes through the same pointer
constexpr int multiple_deref_assigns() {
	int x = 0;
	int* p = &x;
	*p = 1;
	*p = 2;
	*p = 3;
	return x;
}
static_assert(multiple_deref_assigns() == 3);

int main() {
	return simple_deref_assign();   // returns 5
}
