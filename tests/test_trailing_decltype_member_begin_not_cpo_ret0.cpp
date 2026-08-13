// Trailing decltype(c.begin()) must use member lookup even when a same-named
// CPO object is in scope. The 16-byte range vs 8-byte iterator makes a wrong
// CPO-object return visible to codegen (declared int vs struct also fails).
struct Cpo {
	long long a;
	long long b;
};

inline constexpr Cpo begin{};

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

template <class C>
auto std_begin(C& c) -> decltype(c.begin()) {
	return c.begin();
}

int main() {
	int xs[2] = {3, 4};
	Range r{xs, xs + 2};
	Iter it = std_begin(r);
	return it.p == xs ? 0 : 1;
}
