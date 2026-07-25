// Class-template friend operator+ returning the specialization by value.
// Mirrors stdext::checked_array_iterator's friend operator+(diff, const Iter&).
//
// Pattern friends must not be registered for ADL/codegen until [temp.inst]
// substitutes them onto the concrete specialization.

template <class P>
struct Iter {
	P p;
	int i;

	constexpr Iter() : p(nullptr), i(0) {}
	constexpr Iter(P pp, int ii) : p(pp), i(ii) {}

	friend constexpr Iter operator+(int off, const Iter& it) {
		return Iter(it.p, it.i + off);
	}
};

int main() {
	int x = 0;
	Iter<int*> a{&x, 1};
	Iter<int*> b = 2 + a;
	return (b.i == 3 && b.p == &x) ? 0 : 1;
}
