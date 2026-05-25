// Regression test: std::bool_constant<false> and std::bool_constant<true> as base classes
// Previously failed with "Base class 'std::bool_constant' not found" because
// parse_qualified_identifier_with_templates consumed the <> but then only tried
// try_instantiate_class_template which skips alias templates.
namespace std {
template <class T, T Value>
struct integral_constant {
	static constexpr T value = Value;
	using value_type = T;
	using type = integral_constant;

	constexpr operator value_type() const noexcept { return value; }
	constexpr value_type operator()() const noexcept { return value; }
};

template <bool Value>
using bool_constant = integral_constant<bool, Value>;

using false_type = bool_constant<false>;
using true_type = bool_constant<true>;
} // namespace std

struct AlwaysFalse : public std::bool_constant<false> {};
struct AlwaysTrue : public std::bool_constant<true> {};

// Also test the alias chain: bool_constant -> __bool_constant -> integral_constant
struct MyFalse : public std::false_type {};
struct MyTrue : public std::true_type {};

int main() {
    static_assert(AlwaysFalse::value == false);
    static_assert(AlwaysTrue::value == true);
    static_assert(MyFalse::value == false);
    static_assert(MyTrue::value == true);
    return 0;
}
