// Regression: member operator() auto return of a class-type iterator must
// not be deduced as the range parameter type. A 16-byte range vs 8-byte
// iterator makes a wrong deduced return visible to codegen.
struct Iter {
	int* p;
};

struct Range {
	int* first;
	int* last;
	Iter begin() {
		return Iter{first};
	}
};

struct BeginCpo {
	template <class T>
	constexpr auto operator()(T&& value) const {
		return value.begin();
	}
};

inline constexpr BeginCpo begin_cpo{};

int main() {
	int xs[2] = {3, 4};
	Range r{xs, xs + 2};
	Iter it = begin_cpo(r);
	return it.p == xs ? 0 : 1;
}
