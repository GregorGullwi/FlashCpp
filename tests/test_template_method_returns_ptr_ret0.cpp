// Regression test: lazy-materialized member function with pointer return type
// Hold<int*>::get() must return the full 64-bit pointer, not a truncated 32-bit int.
template <typename I>
struct Hold {
	I ptr;
	Hold(I p) : ptr(p) {}
	I get() const { return ptr; }
	void set(I p) { ptr = p; }
};

int main() {
	int arr[3] = {10, 20, 30};
	Hold<int*> h(arr);

	int* p = h.get();
	if (p != arr)
		return 1;

	int x = 99;
	h.set(&x);
	int* q = h.get();
	if (q != &x)
		return 2;

	if (*q != 99)
		return 3;

	return 0;
}
