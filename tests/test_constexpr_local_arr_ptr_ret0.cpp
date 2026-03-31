// Test: constexpr pointer arithmetic on LOCAL arrays inside constexpr function bodies.
// This covers the case where the array only lives in bindings, not the symbol table.

constexpr int deref_local_zero() {
	int arr[] = {10, 20, 30};
	const int* p = &arr[0];
	return *p;  // offset 0 → arr[0]
}
static_assert(deref_local_zero() == 10);

constexpr int deref_local_nonzero() {
	int arr[] = {10, 20, 30};
	const int* p = &arr[0];
	return *(p + 1);	 // offset 1 → arr[1]
}
static_assert(deref_local_nonzero() == 20);

constexpr int subscript_local_ptr() {
	int arr[] = {10, 20, 30};
	const int* p = &arr[0];
	return p[2];	 // ptr[2] = arr[2]
}
static_assert(subscript_local_ptr() == 30);

constexpr int subscript_offset_ptr() {
	int arr[] = {10, 20, 30};
	const int* p = &arr[1];	// p points to arr[1]
	return p[1];	 // ptr[1] = arr[2]
}
static_assert(subscript_offset_ptr() == 30);

int main() { return 0; }
