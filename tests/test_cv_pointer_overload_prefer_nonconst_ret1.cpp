// Test: when called with int*, f(int*) should be preferred over f(const int*).
//
// Per C++20 [over.ics.rank]/3.2.6: identity (T*->T*) is a better conversion
// sequence than qualification adjustment (T*->const T*) within the ExactMatch
// category. The non-const overload must win even when declared AFTER the const one.

int f(const int* p) { return 2; }
int f(int* p) { return 1; }

int main() {
	int val = 42;
	int* ptr = &val;
	return f(ptr);
}
