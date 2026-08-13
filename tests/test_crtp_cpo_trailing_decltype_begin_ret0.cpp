// CRTP view-interface shape: auto& self = cast(); then a same-named CPO
// call begin(self). The CPO operator() must instantiate with the concrete
// derived type, and std::begin-style trailing decltype(c.begin()) must
// return the member iterator rather than the CPO object or int.
namespace access {
struct BeginCpo {
	template <class T>
	constexpr auto operator()(T&& value) const {
		return value.begin();
	}
};

struct EndCpo {
	template <class T>
	constexpr auto operator()(T&& value) const {
		return value.end();
	}
};

inline constexpr BeginCpo begin{};
inline constexpr EndCpo end{};

template <class D>
struct view_iface {
	D& cast() {
		return static_cast<D&>(*this);
	}

	constexpr bool empty() {
		auto& self = cast();
		return access::begin(self) == access::end(self);
	}
};

template <class It>
struct subrange : view_iface<subrange<It>> {
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
auto std_end(C& c) -> decltype(c.end()) {
	return c.end();
}

int main() {
	int xs[2] = {1, 2};
	access::subrange<int*> s{xs, xs + 2};
	int* first = std_begin(s);
	int* last = std_end(s);
	return first == xs && last == xs + 2 ? 0 : 1;
}
