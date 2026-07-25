// Name a related class-template specialization from a member function, forcing
// complete layout of both specializations (incomplete `$hash` placeholders must
// not suppress [temp.inst] completion).

template <class P>
struct Iter {
	P p;
	int i;

	constexpr Iter() : p(nullptr), i(0) {}
	constexpr Iter(P pp, int ii) : p(pp), i(ii) {}

	constexpr Iter<const int*> as_const() const {
		return Iter<const int*>{p, i};
	}
};

int main() {
	int x = 0;
	Iter<int*> a{&x, 1};
	Iter<const int*> c = a.as_const();
	return (c.i == 1 && c.p == &x) ? 0 : 1;
}
