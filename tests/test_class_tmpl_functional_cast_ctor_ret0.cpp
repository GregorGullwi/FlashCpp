// Functional cast / temporary of a class-template specialization:
//   T<Args>(ctor_args)
// must construct T<Args>, not look up T as a function template (C++20 [expr.type.conv] /
// [temp.names] / [temp.local]). Also forces complete layout of both specializations
// (incomplete `$hash` placeholders must not suppress [temp.inst] completion).

template <class P>
struct Iter {
	P p;
	int i;

	constexpr Iter() : p(nullptr), i(0) {}
	constexpr Iter(P pp, int ii) : p(pp), i(ii) {}

	constexpr Iter<const int*> as_const() const {
		return Iter<const int*>(p, i);
	}

	// Without naming the specialization in the return type first.
	constexpr auto as_const_auto() const {
		return Iter<const int*>(p, i);
	}
};

int main() {
	int x = 0;
	Iter<int*> a{&x, 1};
	Iter<const int*> c = a.as_const();
	auto d = a.as_const_auto();
	return (c.i == 1 && c.p == &x && d.i == 1 && d.p == &x) ? 0 : 1;
}
