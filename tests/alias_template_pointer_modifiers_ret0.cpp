template<class T, T v>
struct integral_constant {
	static constexpr T value = v;
	using type = integral_constant;
};

using true_type = integral_constant<bool, true>;
using false_type = integral_constant<bool, false>;

template<class T>
using identity_t = T;

template<class T>
using add_pointer_t = T*;

template<class>
struct is_pointer_helper : false_type {};

template<class T>
struct is_pointer_helper<T*> : true_type {};

template<class T>
struct is_pointer : is_pointer_helper<identity_t<T>>::type {};

template<class T>
struct add_pointer_is_pointer : is_pointer_helper<add_pointer_t<T>>::type {};

static_assert(is_pointer<int*>::value);
static_assert(!is_pointer<int>::value);
static_assert(add_pointer_is_pointer<int>::value);
static_assert(add_pointer_is_pointer<int*>::value);

int main() {
	return 0;
}
