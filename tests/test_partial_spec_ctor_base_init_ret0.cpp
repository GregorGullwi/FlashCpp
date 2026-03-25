// Test that base class initializers in partial specialization constructors
// are correctly copied and substituted in the StructTypeInfo path.

struct Base {
	int val;
	Base(int v) : val(v) {}
};

// Primary template
template<typename T>
struct Derived : Base {
	T extra;
	Derived(int v, T e) : Base(v), extra(e) {}
};

// Partial specialization — constructor calls Base(v) via base initializer
template<typename T>
struct Derived<T*> : Base {
	T deref_extra;
	Derived(int v, T e) : Base(v), deref_extra(e) {}
};

int main() {
	// Primary template
	Derived<char> d1(10, 'A');
	if (d1.val != 10) return 1;
	if (d1.extra != 'A') return 2;

	// Partial specialization — base initializer must survive StructTypeInfo copy
	Derived<int*> d2(42, 7);
	if (d2.val != 42) return 3;
	if (d2.deref_extra != 7) return 4;

	Derived<char*> d3(99, 'Z');
	if (d3.val != 99) return 5;
	if (d3.deref_extra != 'Z') return 6;

	return 0;
}
