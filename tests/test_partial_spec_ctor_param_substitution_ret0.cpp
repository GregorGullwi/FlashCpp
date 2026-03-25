// Test that constructor parameters in partial specializations get template
// parameter substitution.  The pattern-based instantiation path copies
// constructor parameters; if it forgets to substitute T -> concrete type
// the constructor will have the wrong parameter type and codegen may fail
// or produce incorrect results.

// Primary template
template<typename T>
struct Holder {
	T value;
	Holder(T v) : value(v) {}
	T get() { return value; }
};

// Partial specialization for pointer types
// The constructor takes T* (the deduced T from T*), which must be substituted
// with the concrete type when Holder<int*> is instantiated.
template<typename T>
struct Holder<T*> {
	T pointee;
	Holder(T v) : pointee(v) {}
	T get() { return pointee; }
};

int main() {
	// Primary template: Holder<int>, ctor takes int
	Holder<int> h1(10);
	if (h1.get() != 10) return 1;

	// Partial specialization: Holder<int*> matches Holder<T*> with T=int
	// Constructor should take int (not T which is unsubstituted)
	Holder<int*> h2(42);
	if (h2.get() != 42) return 2;

	// Another specialization: Holder<char*> matches Holder<T*> with T=char
	Holder<char*> h3('A');
	if (h3.get() != 'A') return 3;

	return 0;
}
