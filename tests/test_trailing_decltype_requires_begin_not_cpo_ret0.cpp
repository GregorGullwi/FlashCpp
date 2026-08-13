// Reduced non-std stand-in for std::ranges::subrange::begin overloads that
// are mutually exclusive via requires. Trailing decltype(c.begin()) must
// select the satisfied overload and return the iterator, not a same-named
// CPO object against a declared int return.
template <class T>
concept Copyable = true;

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

	constexpr It begin() const
		requires Copyable<It>
	{
		return first;
	}

	constexpr It begin()
		requires (!Copyable<It>)
	{
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

int main() {
	int xs[2] = {1, 2};
	access::subrange<int*> s{xs, xs + 2};
	int* first = std_begin(s);
	int* last = std_end(s);
	const access::subrange<int*>& cs = s;
	int* cfirst = std_begin(cs);
	return first == xs && last == xs + 2 && cfirst == xs ? 0 : 1;
}
