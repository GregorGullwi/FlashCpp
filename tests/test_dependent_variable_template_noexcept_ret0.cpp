// Regression: a class-template instantiation must preserve a dependent
// variable-template noexcept expression on a nested function template.

template <typename...>
using void_t = void;

template <typename Left, typename Right>
constexpr bool same_type = false;

template <typename Type>
constexpr bool same_type<Type, Type> = true;

template <typename Type, typename = void>
constexpr bool has_matching_marker = false;

template <typename Type>
constexpr bool has_matching_marker<Type, void_t<typename Type::marker>> =
	same_type<Type, typename Type::marker>;

template <typename Outer>
struct Wrapper {
	template <typename Inner>
	static void probe(Inner) noexcept(has_matching_marker<Inner>) {}
};

struct Marked {
	using marker = Marked;
};

int main() {
	Wrapper<int>::probe(Marked{});
	return noexcept(Wrapper<int>::probe(Marked{})) ? 0 : 1;
}
