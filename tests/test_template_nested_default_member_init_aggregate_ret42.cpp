// Phase D test: brace-initialized nested struct with default member initializer.
// Aggregate initialization with {} should still use default member initializer
// values (C++20 designated init / aggregate init with default member init).
template <int N>
struct Outer {
	struct Inner {
		int tag = N;
		int extra = N + 10;
	};
};

int main() {
	Outer<16>::Inner obj1{};            // aggregate init, defaults apply
	Outer<16>::Inner obj2;              // default construction, defaults apply

	if (obj1.tag != 16) return 1;
	if (obj1.extra != 26) return 2;
	if (obj2.tag != 16) return 3;
	if (obj2.extra != 26) return 4;

	return obj1.tag + obj1.extra;  // 16 + 26 = 42
}
