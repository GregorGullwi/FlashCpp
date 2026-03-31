// Test: Dependent template placeholder detection in base class arguments
// When template<typename T> struct A : B<C<T>>::type {}, the argument C<T>
// creates a dependent placeholder that must be recognized and deferred.
template <bool V>
struct integral_constant {
	static constexpr bool value = V;
	using type = integral_constant;
};
using true_type = integral_constant<true>;
using false_type = integral_constant<false>;

template <bool C>
using bool_constant = integral_constant<C>;

template <typename P>
struct negate_trait : bool_constant<!bool(P::value)> {};

template <typename T>
struct is_void_custom : false_type {};
template <>
struct is_void_custom<void> : true_type {};

// This uses a dependent template placeholder as arg to negate_trait
// The key test is that this compiles without "Base class not found" error
template <typename T>
struct is_not_void : negate_trait<is_void_custom<T>>::type {};

// Instantiate the template to trigger deferred base resolution
template struct is_not_void<int>;

int main() {
	return 0;
}
