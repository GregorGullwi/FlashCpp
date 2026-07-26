// Regression: partial-spec out-of-line constructor templates must be recognized
// when the owner template name is namespace-qualified (e.g. ns::tuple) and the
// constructor shares the class injection name.
namespace ns {

struct _Exact_tag {};

template<class...>
struct tuple;

template<>
struct tuple<> {};

template<class T, class... Rest>
struct tuple<T, Rest...> : tuple<Rest...> {
	template<class Tag, class Arg>
	tuple(Tag, Arg&&);

	constexpr tuple(T value, Rest... rest)
		: tuple<Rest...>(rest...), value_(value) {}

	T value_;
};

template<class T, class... Rest>
template<class Tag, class Arg>
tuple<T, Rest...>::tuple(Tag, Arg&& arg)
	: tuple<Rest...>(), value_(static_cast<T>(arg)) {}

} // namespace ns

int main() {
	ns::tuple<int> single(7);
	return single.value_ == 7 ? 0 : 1;
}
