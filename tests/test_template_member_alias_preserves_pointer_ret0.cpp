// Regression test for member type aliases that forward template parameters.
//
// Bug: instantiating a member alias such as identity<T>::type preserved only
// the substituted TypeIndex category and dropped TemplateTypeArg modifiers, so
// identity<int*>::type became plain int.  libstdc++ <type_traits> then made
// std::is_pointer<int*>::value select the primary false_type helper instead of
// the T* partial specialization.

template<typename T, T v>
struct integral_constant {
	static constexpr T value = v;
	using value_type = T;
	using type = integral_constant<T, v>;
};

template<bool v>
using bool_constant = integral_constant<bool, v>;

using true_type = bool_constant<true>;
using false_type = bool_constant<false>;

template<typename T>
struct identity {
	using type = T;
};

template<typename T>
using identity_t = typename identity<T>::type;

template<typename>
struct is_pointer_helper : false_type {};

template<typename T>
struct is_pointer_helper<T*> : true_type {};

template<typename T>
struct is_pointer : is_pointer_helper<identity_t<T>>::type {};

static_assert(is_pointer<int*>::value);
static_assert(!is_pointer<int>::value);

int main() {
	return is_pointer<int*>::value && !is_pointer<int>::value ? 0 : 1;
}
