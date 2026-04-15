// Regression test: sizeof(typename ConcreteStruct<T>::member) must parse
// correctly in nested requirements.  Before the fix, the parser rejected this
// with "Expected type or expression after 'sizeof('" because the heuristic
// that falls through to expression parsing fired for any struct whose base
// name was in the type map, even when 'typename' was explicitly written.

template <typename T>
struct wrapper {
using value_type = T;
};

template <typename T>
struct pair_wrapper {
using first = T;
using second = T;
};

// Concrete struct name (not a TTP) in sizeof inside nested requirement
template <typename T>
concept has_sized_value_type = requires {
requires sizeof(typename wrapper<T>::value_type) == sizeof(T);
};

template <typename T>
concept has_matching_pair = requires {
requires sizeof(typename pair_wrapper<T>::first) == sizeof(typename pair_wrapper<T>::second);
};

int main() {
// Both concepts must be satisfied
	if (!has_sized_value_type<int>) return 1;
	if (!has_sized_value_type<long long>) return 2;
	if (!has_matching_pair<int>) return 3;
	if (!has_matching_pair<double>) return 4;
	return 0;
}
