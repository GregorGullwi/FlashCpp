// Regression: namespaced partial-specialization out-of-line constructor
// templates must be recognized as constructor stubs (unqualified ctor name
// vs qualified pattern key, e.g. `tuple` vs `lib::Tuple`) and attached through
// source-member identity so same-name overloads bind correctly.
// Reduced from MSVC <tuple>: `tuple<_This, _Rest...>::tuple(...)`.
namespace lib {
template<class... Ts>
struct Tuple;

template<class This, class... Rest>
struct Tuple<This, Rest...> {
	int which;

	template<class U>
	Tuple(This*, U);

	template<class U>
	Tuple(long*, U);
};

template<class This, class... Rest>
template<class U>
Tuple<This, Rest...>::Tuple(This*, U)
	: which(0) {
	which = 2;
}

template<class This, class... Rest>
template<class U>
Tuple<This, Rest...>::Tuple(long*, U)
	: which(0) {
	which = 1;
}
}

int main() {
	int value = 0;
	long long_value = 0;

	lib::Tuple<int, float> from_t_ptr(&value, 0);
	if (from_t_ptr.which != 2) {
		return 1;
	}

	lib::Tuple<int, float> from_long_ptr(&long_value, 0);
	if (from_long_ptr.which != 1) {
		return 2;
	}

	return 0;
}
