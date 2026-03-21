// Test: when called with const int*, only the const overload is viable.
//
// f(int*) would require removing const from the pointee - not allowed.
// f(const int*) is an exact match (identity on const int*).
// Only overload (2) is viable -> returns 2.

int f(int* p) { return 1; }
int f(const int* p) { return 2; }

int main() {
	const int val = 42;
	const int* ptr = &val;
	return f(ptr);
}
