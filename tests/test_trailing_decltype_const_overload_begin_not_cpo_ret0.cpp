// Reduced non-std stand-in for std::begin(std::ranges::subrange&):
// trailing decltype(c.begin()) must select the const/non-const member
// overload and return the iterator, not a same-named CPO object typed as int.
namespace access {
struct Cpo {
	long long a;
	long long b;
};

inline constexpr Cpo begin{};
inline constexpr Cpo end{};

template <class It>
struct subrange {
	It first;
	It last;

	constexpr It begin() const {
		return first;
	}

	constexpr It begin() {
		return first;
	}

	constexpr It end() const {
		return last;
	}
};
} // namespace access

template <class C>
auto std_begin(C& c) -> decltype(c.begin()) {
	return c.begin();
}

template <class C>
auto std_begin(const C& c) -> decltype(c.begin()) {
	return c.begin();
}

template <class C>
auto std_end(C& c) -> decltype(c.end()) {
	return c.end();
}

template <class C>
auto std_end(const C& c) -> decltype(c.end()) {
	return c.end();
}

int main() {
	int xs[2] = {1, 2};
	access::subrange<int*> s{xs, xs + 2};
	int* first = std_begin(s);
	int* last = std_end(s);
	const access::subrange<int*>& cs = s;
	int* cfirst = std_begin(cs);
	return first == xs && last == xs + 2 && cfirst == xs ? 0 : 1;
}
